/*
 * NAME:
 *      sigcompat - BSD compat signals via POSIX
 *
 * SYNOPSIS:
 *	void (*signal(int  "sig", void (*"handler")(int)))(int);
 *	int sigsetmask(int "mask");
 *	int sigblock(int "mask");
 *	int sigpause(int "mask");
 *	int sigvec(int "signo", struct sigvec *"sv", struct sigvec *"osv");
 *
 * DESCRIPTION:
 *	These implement the old BSD routines via the POSIX equivalents.
 *	This module can be used to provide the missing routines, or if
 *	'FORCE_POSIX_SIGNALS' is defined, force use of these.
 *
 *	Note that signal() is identical to my Signal() routine except
 *	for checking for recursion.  Within libsig, signal() just
 *	calls Signal().
 *
 * BUGS:
 *      This package assumes POSIX signal handling is available and
 *	NOT implemeneted using these routines.  To be safe, we check
 *	for recursion and abort(3) if detected.
 *
 *	Sadly, on some systems, sigset_t is an array, and we cannot
 *	test for this via #if sizeof(sigset_t) ..., so unless
 *	'SIGSET_T_INT' is defined, we have to assume the worst and use
 *	memcpy(3) to handle args and return values.
 *
 * HISTORY:
 *	These routines originate from BSD, and are derrived from the
 *	NetBSD 1.1 implementation.  They have been seriously hacked to
 *	make them portable to other systems.
 *
 * AUTHOR:
 *      Simon J. Gerraty <sjg@crufty.net>
 */
/*
 *      @(#)Copyright (c) 1994, Simon J. Gerraty.
 *
 *      This is free software.  It comes with NO WARRANTY.
 *      Permission to use, modify and distribute this source code
 *      is granted subject to the following conditions.
 *      1/ that the above copyright notice and this notice
 *      are preserved in all copies and that due credit be given
 *      to the author.
 *      2/ that any changes to this code are clearly commented
 *      as such so that the author does not get blamed for bugs
 *      other than his own.
 *
 *      Please send copies of changes and bug-fixes to:
 *      sjg@crufty.net
 */

/*
 * Copyright (c) 1989 The Regents of the University of California.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <signal.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#if defined(sun) && !(defined(__svr4__) || defined(__SVR4))
# define NO_SIGCOMPAT
#endif
#if defined(__MINT__)
# define NO_SIGCOMPAT
#endif

#if !defined(NO_SIGCOMPAT) && (defined(HAVE_SIGACTION) || defined(SA_NOCLDSTOP))

#if defined(LIBC_SCCS) && !defined(lint)
/*static char *sccsid = "from: @(#)sigcompat.c	5.3 (Berkeley) 2/24/91";*/
static char *rcsid = "$Id: sigcompat.c,v 1.23 2011/02/14 00:07:11 sjg Exp $";
#endif				/* LIBC_SCCS and not lint */

#undef signal
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/cdefs.h>
#include "assert.h"

#ifndef ASSERT
# define ASSERT assert
#endif

#ifdef NDEBUG
# define _DBUG(x)
#else
# define _DBUG(x) x
#endif

#ifndef SA_RESTART
# define SA_RESTART	2
#endif
#ifndef SV_INTERRUPT
# define SV_INTERRUPT SA_RESTART
#endif

#ifndef MASK_T
# if defined(__hpux__) || defined(__hpux)
#   define MASK_T long
# else
#   define MASK_T int
# endif
#endif
/* I just hate HPsUX */
#if (defined(__HPUX_VERSION) && __HPUX_VERSION > 9) || defined(__hpux)
# define  PAUSE_MASK_T int
#else
# define PAUSE_MASK_T MASK_T
#endif

#ifndef SIG_HDLR
# define SIG_HDLR void
#endif

#ifdef FORCE_POSIX_SIGNALS
#if !(defined(libsig) || defined(libsjg))
/*
 * This little block is almost identical to Signal(),
 * and make this module standalone.
 * We don't use it in libsig by default, as some apps might use both
 * and expect _SignalFlags to be used by both.
 */

#ifndef SIGNAL_FLAGS
# define SIGNAL_FLAGS 0		/* no auto-restart */
#endif
int     _signalFlags = SIGNAL_FLAGS;

SIG_HDLR(*signal(int sig, SIG_HDLR(*handler)(int)))(int)
{
	_DBUG(static int depth_signal = 0);
	struct sigaction act, oact;
	int     n;

	_DBUG(++depth_signal);
	ASSERT(depth_signal < 2);
	act.sa_handler = handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = _signalFlags;
	n = sigaction(sig, &act, &oact);
	_DBUG(--depth_signal);
	if (n < 0)
		return (SIG_ERR);
	return (oact.sa_handler);
}
#else
SIG_HDLR(*signal(int sig, SIG_HDLR(*handler)(int)))(int)
{
	extern  SIG_HDLR(*Signal(int, void (*)(int)))(int);
	_DBUG(static int depth_signal = 0);
	SIG_HDLR(*old) __P((int));

	_DBUG(++depth_signal);
	ASSERT(depth_signal < 2);
	old = Signal(sig, handler);
	_DBUG(--depth_signal);
	return old;
}
#endif
#endif

/*
 * on some systems, sigset_t is an array...
 * it would be nicer if we could do
 * #if sizeof(sigset_t) > sizeof(MASK_T)
 */
#ifdef SIGSET_T_INT
# define ss2m(ss) (MASK_T) *(ss)
# define m2ss(ss, m)	*ss = (sigset_t) *(m)
#else
static  MASK_T
ss2m(sigset_t *ss)
{
	MASK_T  ma[(sizeof(sigset_t) / sizeof(MASK_T)) + 1];

	memcpy((char *) ma, (char *) ss, sizeof(sigset_t));
	return ma[0];
}

static void
m2ss(sigset_t *ss, MASK_T *m)
{
	if (sizeof(sigset_t) > sizeof(MASK_T))
		memset((char *) ss, 0, sizeof(sigset_t));

	memcpy((char *) ss, (char *) m, sizeof(MASK_T));
}
#endif

#if !defined(HAVE_SIGSETMASK) || defined(FORCE_POSIX_SIGNALS)
MASK_T
sigsetmask(MASK_T mask)
{
	_DBUG(static int depth_sigsetmask = 0);
	sigset_t m, omask;
	int     n;

	_DBUG(++depth_sigsetmask);
	ASSERT(depth_sigsetmask < 2);
	m2ss(&m, &mask);
	n = sigprocmask(SIG_SETMASK, (sigset_t *) & m, (sigset_t *) & omask);
	_DBUG(--depth_sigsetmask);
	if (n)
		return (n);

	return ss2m(&omask);
}


MASK_T
sigblock(MASK_T mask)
{
	_DBUG(static int depth_sigblock = 0);
	sigset_t m, omask;
	int     n;

	_DBUG(++depth_sigblock);
	ASSERT(depth_sigblock < 2);
	if (mask)
		m2ss(&m, &mask);
	n = sigprocmask(SIG_BLOCK, (sigset_t *) ((mask) ? &m : 0), (sigset_t *) & omask);
	_DBUG(--depth_sigblock);
	if (n)
		return (n);
	return ss2m(&omask);
}

#undef sigpause				/* Linux at least */

PAUSE_MASK_T
sigpause(PAUSE_MASK_T mask)
{
	_DBUG(static int depth_sigpause = 0);
	sigset_t m;
	PAUSE_MASK_T  n;

	_DBUG(++depth_sigpause);
	ASSERT(depth_sigpause < 2);
	m2ss(&m, &mask);
	n = sigsuspend(&m);
	_DBUG(--depth_sigpause);
	return n;
}
#endif

#if defined(HAVE_SIGVEC) && defined(FORCE_POSIX_SIGNALS)
int
sigvec(int signo, struct sigvec *sv, struct sigvec *osv)
{
	_DBUG(static int depth_sigvec = 0);
	int     ret;
	struct sigvec nsv;

	_DBUG(++depth_sigvec);
	ASSERT(depth_sigvec < 2);
	if (sv) {
		nsv = *sv;
		nsv.sv_flags ^= SV_INTERRUPT;	/* !SA_INTERRUPT */
	}
	ret = sigaction(signo, sv ? (struct sigaction *) & nsv : NULL,
	    (struct sigaction *) osv);
	_DBUG(--depth_sigvec);
	if (ret == 0 && osv)
		osv->sv_flags ^= SV_INTERRUPT;	/* !SA_INTERRUPT */
	return (ret);
}
#endif

#ifdef MAIN
# ifndef sigmask
#   define sigmask(n) ((unsigned int)1 << (((n) - 1) & (32 - 1)))
# endif

int
main(int  argc, char *argv[])
{
	MASK_T  old = 0;

	printf("expect: old=0,old=2\n");
	fflush(stdout);
	signal(SIGQUIT, SIG_IGN);
	old = sigblock(sigmask(SIGINT));
	printf("old=%d,", old);
	old = sigsetmask(sigmask(SIGALRM));
	printf("old=%d\n", old);
}
#endif
#endif
