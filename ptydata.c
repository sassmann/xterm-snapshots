/*
 * $XFree86: xc/programs/xterm/ptydata.c,v 1.6 1999/06/27 14:08:41 dawes Exp $
 */

/************************************************************

Copyright 1999 by Thomas E. Dickey <dickey@clark.net>

                        All Rights Reserved

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE ABOVE LISTED COPYRIGHT HOLDER(S) BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name(s) of the above copyright
holders shall not be used in advertising or otherwise to promote the
sale, use or other dealings in this Software without prior written
authorization.

********************************************************/

#include <data.h>

/*
 * Check for both EAGAIN and EWOULDBLOCK, because some supposedly POSIX
 * systems are broken and return EWOULDBLOCK when they should return EAGAIN.
 * Note that this macro may evaluate its argument more than once.
 */
#if defined(EAGAIN) && defined(EWOULDBLOCK)
#define E_TEST(err) ((err) == EAGAIN || (err) == EWOULDBLOCK)
#else
#ifdef EAGAIN
#define E_TEST(err) ((err) == EAGAIN)
#else
#define E_TEST(err) ((err) == EWOULDBLOCK)
#endif
#endif

#define UTF8_FLAG       0x80000000U
#define UTF8_CODE(code) ((code) | (screen->utf8_controls ? 0 : UTF8_FLAG))

int getPtyData(TScreen *screen, fd_set *select_mask, PtyData *data)
{
    int i;

#ifndef AMOEBA
    if (FD_ISSET (screen->respond, select_mask))
#else
    if ((data->cnt = cb_full(screen->tty_outq)) > 0)
#endif
    {
#ifdef ALLOWLOGGING
	if (screen->logging)
	    FlushLog(screen);
#endif
	/* set data->ptr here, in case we need it outside this chunk */
	data->ptr = DecodedData(data);
#ifndef AMOEBA
	data->cnt = read(screen->respond, (char *) data->buf, BUF_SIZE);
#else
	if ((data->cnt = cb_gets(screen->tty_outq, data->buf, data->cnt, BUF_SIZE)) == 0) {
	    errno = EIO;
	    data->cnt = -1;
	}
#endif
	if (data->cnt <= 0) {
	    /*
	     * Yes, I know this is a majorly f*ugly hack, however it seems to
	     * be necessary for Solaris x86.  DWH 11/15/94
	     * Dunno why though..
	     */
#if defined(i386) && defined(SVR4) && defined(sun)
	    if (errno == EIO || errno == 0 )
#else
	    if (errno == EIO)
#endif
		Cleanup (0);
	    else if (!E_TEST(errno))
		Panic("input: read returned unexpected error (%d)\n", errno);
	} else if (data->cnt == 0) {
#if defined(MINIX) || defined(__EMX__)
	    Cleanup(0);
#else
	    Panic("input: read returned zero\n", 0);
#endif
	} else {
#if OPT_WIDE_CHARS
	    if (screen->utf8_mode) {
		int j = 0;
		for (i = 0; i < data->cnt; i++) {
		    unsigned c = data->buf[i];
		    /* Combine UTF-8 into Unicode */
		    if (c < 0x80) {
			if (screen->utf_count > 0)
			    data->buf2[j++] = 0xfffd;
			data->buf2[j++] = c;
			screen->utf_count = 0;
		    } else if (c < 0xc0) {
			if (screen->utf_count < 1) {
			    data->buf2[j++] = 0xfffd;
			} else {
			    if (screen->utf_char > 0x03ff) {
				/* value would be >0xffff */
				screen->utf_char = 0xfffd;
			    } else {
			      screen->utf_char <<= 6;
			      screen->utf_char |= (c & 0x3f);
			    }
			    screen->utf_count--;
			    if (screen->utf_count == 0)
				data->buf2[j++] = UTF8_CODE(c = screen->utf_char);
			}
		    } else {
			if (screen->utf_count > 0)
			    data->buf2[j++] = 0xfffd;
			if (c < 0xe0) {
			    screen->utf_count = 1;
			    screen->utf_char = (c & 0x1f);
			} else if (c < 0xf0) {
			    screen->utf_count = 2;
			    screen->utf_char = (c & 0x0f);
			} else if (c < 0xf8) {
			    screen->utf_count = 3;
			    screen->utf_char = (c & 0x07);
			} else if (c < 0xfc) {
			    screen->utf_count = 4;
			    screen->utf_char = (c & 0x03);
			} else if (c < 0xfe) {
			    screen->utf_count = 5;
			    screen->utf_char = (c & 0x01);
			} else {
			    data->buf2[j++] = 0xfffd;
			    screen->utf_count = 0;
			}
		    }
		}
		TRACE(("UTF-8 count %d, char %04X input %d/%d bytes\n",
			screen->utf_count,
			screen->utf_char,
			data->cnt, j))
		data->cnt = j;
	    } else {
		for (i = 0; i < data->cnt; i++)
		    data->ptr[i] = data->buf[i];
	    }
#endif
	    /* read from pty was successful */
	    if (!screen->output_eight_bits) {
		for (i = 0; i < data->cnt; i++) {
		    data->ptr[i] &= 0x7f;
		}
	    }
#if OPT_TRACE
	    for (i = 0; i < data->cnt; i++) {
		if (!(i%8)) TRACE(("%s", i ? "\n    " : "READ"))
		TRACE((" %c%04X",
			(UTF8_FLAG & data->ptr[i]) ? '*' : ' ',
			data->ptr[i] & ~UTF8_FLAG))
	    }
	    TRACE(("\n"))
#endif
	    return (data->cnt);
	}
    }
    return 0;
}

int morePtyData(PtyData *data)
{
    return (data->cnt > 0);
}

int nextPtyData(PtyData *data)
{
    (data->cnt)--;
    return(*(data->ptr)++);
}

void initPtyData(PtyData *data)
{
    data->cnt = 0;
    data->ptr = DecodedData(data);
}

/*
 * Tells how much we have used out of the current buffer
 */
unsigned usedPtyData(PtyData *data)
{
    return (data->ptr - DecodedData(data));
}

#if OPT_WIDE_CHARS
Char * convertToUTF8(Char *lp, unsigned c)
{
    if (c < 0x80) {		/*  0*******  */
	*lp++ = (c);
    } else if (c < 0x800) {	/*  110***** 10******  */
	*lp++ = (0xc0 | (c >> 6));
	*lp++ = (0x80 | (c & 0x3f));
    } else {			/*  1110**** 10****** 10******  */
	*lp++ = (0xe0 | (c >> 12));
	*lp++ = (0x80 | ((c >> 6) & 0x3f));
	*lp++ = (0x80 | (c & 0x3f));
    }
    /*
     * UTF-8 is defined for words of up to 31 bits, but we need only 16
     * bits here, since that's all that X11R6 supports.
     */
    return lp;
}

/*
 * Write data back to the PTY
 */
void writePtyData(int f, IChar *d, unsigned len)
{
    static Char *dbuf;
    static unsigned dlen;
    unsigned n = (len << 1);

    if (dlen <= len) {
	dlen = n;
	dbuf = (Char *)XtRealloc((char *)dbuf, dlen);
    }

    for (n = 0; n < len; n++)
	dbuf[n] = d[n];
    v_write(f, dbuf, n);
}
#endif
