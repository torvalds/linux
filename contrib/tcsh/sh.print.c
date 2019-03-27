/* $Header: /p/tcsh/cvsroot/tcsh/sh.print.c,v 3.37 2015/05/10 13:29:28 christos Exp $ */
/*
 * sh.print.c: Primitive Output routines.
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "sh.h"

RCSID("$tcsh: sh.print.c,v 3.37 2015/05/10 13:29:28 christos Exp $")

#include "ed.h"

extern int Tty_eight_bit;

int     lbuffed = 1;		/* true if line buffered */

static	void	p2dig	(unsigned int);

/*
 * C Shell
 */

#if defined(BSDLIMIT) || defined(RLIMIT_CPU)
void
psecs(unsigned long l)
{
    int i;

    i = (int) (l / 3600);
    if (i) {
	xprintf("%d:", i);
	i = (int) (l % 3600);
	p2dig(i / 60);
	goto minsec;
    }
    i = (int) l;
    xprintf("%d", i / 60);
minsec:
    i %= 60;
    xprintf(":");
    p2dig(i);
}

#endif

void			/* PWP: print mm:ss.dd, l is in sec*100 */
#ifdef BSDTIMES
pcsecs(unsigned long l)
#else /* BSDTIMES */
# ifndef POSIX
pcsecs(time_t l)
# else /* POSIX */
pcsecs(clock_t l)
# endif /* POSIX */
#endif /* BSDTIMES */
{
    int i;

    i = (int) (l / 360000);
    if (i) {
	xprintf("%d:", i);
	i = (int) ((l % 360000) / 100);
	p2dig(i / 60);
	goto minsec;
    }
    i = (int) (l / 100);
    xprintf("%d", i / 60);
minsec:
    i %= 60;
    xprintf(":");
    p2dig(i);
    xprintf(".");
    p2dig((int) (l % 100));
}

static void 
p2dig(unsigned i)
{

    xprintf("%u%u", i / 10, i % 10);
}

char    linbuf[2048];		/* was 128 */
char   *linp = linbuf;
int    output_raw = 0;		/* PWP */
int    xlate_cr   = 0;		/* HE */

/* For cleanup_push() */
void
output_raw_restore(void *xorig)
{
    int *orig;

    orig = xorig;
    output_raw = *orig;
}

#ifdef WIDE_STRINGS
void
putwraw(Char c)
{
    char buf[MB_LEN_MAX];
    size_t i, len;
    
    len = one_wctomb(buf, c & CHAR);
    for (i = 0; i < len; i++)
	putraw((unsigned char)buf[i] | (c & ~CHAR));
}

void
xputwchar(Char c)
{
    char buf[MB_LEN_MAX];
    size_t i, len;
    
    len = one_wctomb(buf, c & CHAR);
    for (i = 0; i < len; i++)
	xputchar((unsigned char)buf[i] | (c & ~CHAR));
}
#endif

void
xputchar(int c)
{
    int     atr;

    atr = c & ATTRIBUTES & TRIM;
    c &= CHAR | QUOTE;
    if (!output_raw && (c & QUOTE) == 0) {
	if (iscntrl(c) && (ASC(c) < 0x80 || MB_CUR_MAX == 1)) {
	    if (c != '\t' && c != '\n'
#ifdef COLORCAT
	        && !(adrof(STRcolorcat) && c == CTL_ESC('\033'))
#endif
		&& (xlate_cr || c != '\r'))
	    {
		xputchar('^' | atr);
		if (c == CTL_ESC('\177'))
		    c = '?';
		else
		    /* Note: for IS_ASCII, this compiles to: c = c | 0100 */
		    c = CTL_ESC(ASC(c)|0100);
	    }
	}
	else if (!isprint(c) && (ASC(c) < 0x80 || MB_CUR_MAX == 1)) {
	    xputchar('\\' | atr);
	    xputchar((((c >> 6) & 7) + '0') | atr);
	    xputchar((((c >> 3) & 7) + '0') | atr);
	    c = (c & 7) + '0';
	}
	(void) putraw(c | atr);
    }
    else {
	c &= TRIM;
	if (haderr ? (didfds ? is2atty : isdiagatty) :
	    (didfds ? is1atty : isoutatty))
	    SetAttributes(c | atr);
	(void) putpure(c);
    }
    if (lbuffed && (c & CHAR) == '\n')
	flush();
}

int
putraw(int c)
{
    if (haderr ? (didfds ? is2atty : isdiagatty) :
	(didfds ? is1atty : isoutatty)) {
	if (Tty_eight_bit == -1)
	    ed_set_tty_eight_bit();
	if (!Tty_eight_bit && (c & META)) {
	    c = (c & ~META) | STANDOUT;
	}
	SetAttributes(c);
    }
    return putpure(c);
}

int
putpure(int c)
{
    c &= CHAR;

    *linp++ = (char) c;
    if (linp >= &linbuf[sizeof linbuf - 10])
	flush();
    return (1);
}

void
drainoline(void)
{
    linp = linbuf;
}

void
flush(void)
{
    int unit, oldexitset = exitset;
    static int interrupted = 0;

    /* int lmode; */

    if (linp == linbuf)
	return;
    if (GettingInput && !Tty_raw_mode && linp < &linbuf[sizeof linbuf - 10])
	return;
    if (handle_interrupt)
       exitset = 1;

    if (interrupted) {
	interrupted = 0;
	linp = linbuf;		/* avoid recursion as stderror calls flush */
	if (handle_interrupt)
	    fixerror();
	else
	    stderror(ERR_SILENT);
    }
    interrupted = 1;
    if (haderr)
	unit = didfds ? 2 : SHDIAG;
    else
	unit = didfds ? 1 : SHOUT;
#ifdef COMMENT
#ifdef TIOCLGET
    if (didfds == 0 && ioctl(unit, TIOCLGET, (ioctl_t) & lmode) == 0 &&
	lmode & LFLUSHO) {
	lmode = LFLUSHO;
	(void) ioctl(unit, TIOCLBIC, (ioclt_t) & lmode);
	(void) xwrite(unit, "\n", 1);
    }
#endif
#endif
    if (xwrite(unit, linbuf, linp - linbuf) == -1)
	switch (errno) {
#ifdef EIO
	/* We lost our tty */
	case EIO:
#endif
#ifdef ENXIO
	/*
	 * Deal with Digital Unix 4.0D bogocity, returning ENXIO when
	 * we lose our tty.
	 */
	case ENXIO:
#endif
	/*
	 * IRIX 6.4 bogocity?
	 */
#ifdef ENOTTY
	case ENOTTY:
#endif
#ifdef EBADF
	case EBADF:
#endif
#ifdef ESTALE
	/*
	 * Lost our file descriptor, exit (IRIS4D)
	 */
	case ESTALE:
#endif
#ifdef ENOENT
	/*
	 * Deal with SoFS bogocity: returns ENOENT instead of ESTALE.
	 */
	case ENOENT:
#endif
	/*
	 * Over our quota, writing the history file
	 */
#ifdef EDQUOT
	case EDQUOT:
#endif
	/* Nothing to do, but die */
	    if (handle_interrupt == 0)
		xexit(1);
	    /*FALLTHROUGH*/
	default:
	    if (handle_interrupt)
		fixerror();
	    else
		stderror(ERR_SILENT);
	    break;
	}

    exitset = oldexitset;
    linp = linbuf;
    interrupted = 0;
}
