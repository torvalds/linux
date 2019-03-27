/* $Header: /p/tcsh/cvsroot/tcsh/tc.disc.c,v 3.18 2011/01/09 16:25:29 christos Exp $ */
/*
 * tc.disc.c: Functions to set/clear line disciplines
 *
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

RCSID("$tcsh: tc.disc.c,v 3.18 2011/01/09 16:25:29 christos Exp $")

#ifdef OREO
#include <compat.h>
#endif	/* OREO */

#include "ed.h"

static int add_discipline = 0;	/* Did we add a line discipline	 */

#if defined(IRIS4D) || defined(OREO) || defined(sonyrisc) || defined(__ANDROID__)
# define HAVE_DISC
# ifndef POSIX
static struct termio otermiob;
# else
static struct termios otermiob;
# endif /* POSIX */
#endif	/* IRIS4D || OREO */

#ifdef _IBMR2
# define HAVE_DISC
char    strPOSIX[] = "posix";
#endif	/* _IBMR2 */

#if !defined(HAVE_DISC) && defined(TIOCGETD) && defined(NTTYDISC)
static int oldisc;
#endif /* !HAVE_DISC && TIOCGETD && NTTYDISC */

int
/*ARGSUSED*/
setdisc(int f)
{
#ifdef IRIS4D
# ifndef POSIX
    struct termio termiob;
# else
    struct termios termiob;
# endif

    if (ioctl(f, TCGETA, (ioctl_t) & termiob) == 0) {
	otermiob = termiob;
#if (SYSVREL < 4) || !defined(IRIS4D)
	if (termiob.c_line != NTTYDISC || termiob.c_cc[VSWTCH] == 0) { /*}*/
	    termiob.c_line = NTTYDISC;
#else
	if (termiob.c_cc[VSWTCH] == 0) {
#endif
	    termiob.c_cc[VSWTCH] = CSWTCH;
	    if (ioctl(f, TCSETA, (ioctl_t) & termiob) != 0)
		return (-1);
	}
    }
    else
	return (-1);
    add_discipline = 1;
    return (0);
#endif /* IRIS4D */


#ifdef OREO
# ifndef POSIX
    struct termio termiob;
# else
    struct termios termiob;
# endif

    struct ltchars ltcbuf;

    if (ioctl(f, TCGETA, (ioctl_t) & termiob) == 0) {
	int comp = getcompat(COMPAT_BSDTTY);
	otermiob = termiob;
	if ((comp & COMPAT_BSDTTY) != COMPAT_BSDTTY) {
	    (void) setcompat(comp | COMPAT_BSDTTY);
	    if (ioctl(f, TIOCGLTC, (ioctl_t) & ltcbuf) != 0)
		xprintf(CGETS(21, 1, "Couldn't get local chars.\n"));
	    else {
		ltcbuf.t_suspc = CTL_ESC('\032');        /* ^Z */
		ltcbuf.t_dsuspc = CTL_ESC('\031');       /* ^Y */
		ltcbuf.t_rprntc = CTL_ESC('\022');       /* ^R */
		ltcbuf.t_flushc = CTL_ESC('\017');       /* ^O */
		ltcbuf.t_werasc = CTL_ESC('\027');       /* ^W */
		ltcbuf.t_lnextc = CTL_ESC('\026');       /* ^V */
		if (ioctl(f, TIOCSLTC, (ioctl_t) & ltcbuf) != 0)
		    xprintf(CGETS(21, 2, "Couldn't set local chars.\n"));
	    }
	    termiob.c_cc[VSWTCH] = '\0';
	    if (ioctl(f, TCSETAF, (ioctl_t) & termiob) != 0)
		return (-1);
	}
    }
    else
	return (-1);
    add_discipline = 1;
    return (0);
#endif				/* OREO */


#ifdef _IBMR2
    union txname tx;

    tx.tx_which = 0;

    if (ioctl(f, TXGETLD, (ioctl_t) & tx) == 0) {
	if (strcmp(tx.tx_name, strPOSIX) != 0)
	    if (ioctl(f, TXADDCD, (ioctl_t) strPOSIX) == 0) {
		add_discipline = 1;
		return (0);
	    }
	return (0);
    }
    else
	return (-1);
#endif	/* _IBMR2 */

#ifndef HAVE_DISC
# if defined(TIOCGETD) && defined(NTTYDISC)
    if (ioctl(f, TIOCGETD, (ioctl_t) & oldisc) == 0) {
	if (oldisc != NTTYDISC) {
	    int     ldisc = NTTYDISC;

	    if (ioctl(f, TIOCSETD, (ioctl_t) & ldisc) != 0)
		return (-1);
	    add_discipline = 1;
	}
	else
	    oldisc = -1;
	return (0);
    }
    else
	return (-1);
# else
    USE(f);
    return (0);
# endif	/* TIOCGETD && NTTYDISC */
#endif	/* !HAVE_DISC */
} /* end setdisc */


int
/*ARGSUSED*/
resetdisc(int f)
{
    if (add_discipline) {
	add_discipline = 0;
#if defined(OREO) || defined(IRIS4D)
	return (ioctl(f, TCSETAF, (ioctl_t) & otermiob));
#endif /* OREO || IRIS4D */

#ifdef _IBMR2
	return (ioctl(f, TXDELCD, (ioctl_t) strPOSIX));
#endif /* _IBMR2 */

#ifndef HAVE_DISC
# if defined(TIOCSETD) && defined(NTTYDISC)
	return (ioctl(f, TIOCSETD, (ioctl_t) & oldisc));
# endif /* TIOCSETD && NTTYDISC */
#endif /* !HAVE_DISC */
    }
    USE(f);
    return (0);
} /* end resetdisc */
