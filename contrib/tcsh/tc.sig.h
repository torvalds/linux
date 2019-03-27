/* $Header: /p/tcsh/cvsroot/tcsh/tc.sig.h,v 3.34 2010/11/29 15:28:58 christos Exp $ */
/*
 * tc.sig.h: Signal handling
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
#ifndef _h_tc_sig
#define _h_tc_sig

#if (SYSVREL > 0) || defined(BSD4_4) || defined(_MINIX) || defined(DGUX) || defined(WINNT_NATIVE) || defined(__QNXNTO__)
# include <signal.h>
# ifndef SIGCHLD
#  define SIGCHLD SIGCLD
# endif /* SIGCHLD */
#else /* SYSVREL == 0 */
# include <sys/signal.h>
#endif /* SYSVREL > 0 */

#if defined(__APPLE__) || defined(SUNOS4) || defined(DGUX) || defined(hp800) || (SYSVREL > 3 && defined(VFORK))
# define SAVESIGVEC
#endif /* SUNOS4 || DGUX || hp800 || SVR4 & VFORK */

#if SYSVREL > 0
# ifdef BSDJOBS
/* here I assume that systems that have bsdjobs implement the
 * the setpgrp call correctly. Otherwise defining this would
 * work, but it would kill the world, because all the setpgrp
 * code is the the part defined when BSDJOBS are defined
 * NOTE: we don't want killpg(a, b) == kill(-getpgrp(a), b)
 * cause process a might be already dead and getpgrp would fail
 */
#  define killpg(a, b) kill(-(a), (b))
# else
/* this is the poor man's version of killpg()! Just kill the
 * current process and don't worry about the rest. Someday
 * I hope I get to fix that.
 */
#  define killpg(a, b) kill((a), (b))
# endif /* BSDJOBS */
#endif /* SYSVREL > 0 */

#ifdef _MINIX
# include <signal.h>
# define killpg(a, b) kill((a), (b))
# ifdef _MINIX_VMD
#  define signal(a, b) signal((a), (a) == SIGCHLD ? SIG_IGN : (b))
# endif /* _MINIX_VMD */
#endif /* _MINIX */

#ifdef _VMS_POSIX
# define killpg(a, b) kill(-(a), (b))
#endif /* atp _VMS_POSIX */

#ifdef aiws
# undef	killpg
# define 	killpg(a, b)	kill(-getpgrp(a), b)
#endif /* aiws */

#if !defined(NSIG) && defined(SIGMAX)
# define NSIG (SIGMAX+1)
#endif /* !NSIG && SIGMAX */
#if !defined(NSIG) && defined(_SIG_MAX)
# define NSIG (_SIG_MAX+1)
#endif /* !NSIG && _SIG_MAX */
#if !defined(NSIG) && defined(_NSIG)
# define NSIG _NSIG
#endif /* !NSIG && _NSIG */
#if !defined(NSIG)
#define NSIG (sizeof(sigset_t) * 8)
#endif /* !NSIG */
#if !defined(MAXSIG) && defined(NSIG)
# define MAXSIG NSIG
#endif /* !MAXSIG && NSIG */

/*
 * We choose a define for the window signal if it exists..
 */
#ifdef SIGWINCH
# define SIG_WINDOW SIGWINCH
#else
# ifdef SIGWINDOW
#  define SIG_WINDOW SIGWINDOW
# endif /* SIGWINDOW */
#endif /* SIGWINCH */

#ifdef SAVESIGVEC
# define NSIGSAVED 7
 /*
  * These are not inline for speed. gcc -traditional -O on the sparc ignores
  * the fact that vfork() corrupts the registers. Calling a routine is not
  * nice, since it can make the compiler put some things that we want saved
  * into registers 				- christos
  */
# define savesigvec(sv, sm)			\
    do {					\
	sigset_t m__;				\
						\
	sigaction(SIGINT,  NULL, &(sv)[0]);	\
	sigaction(SIGQUIT, NULL, &(sv)[1]);	\
	sigaction(SIGTSTP, NULL, &(sv)[2]);	\
	sigaction(SIGTTIN, NULL, &(sv)[3]);	\
	sigaction(SIGTTOU, NULL, &(sv)[4]);	\
	sigaction(SIGTERM, NULL, &(sv)[5]);	\
	sigaction(SIGHUP,  NULL, &(sv)[6]);	\
	sigemptyset(&m__);			\
	sigaddset(&m__, SIGINT);		\
	sigaddset(&m__, SIGQUIT);		\
	sigaddset(&m__, SIGTSTP);		\
	sigaddset(&m__, SIGTTIN);		\
	sigaddset(&m__, SIGTTOU);		\
	sigaddset(&m__, SIGTERM);		\
	sigaddset(&m__, SIGHUP);		\
	sigprocmask(SIG_BLOCK, &m__, &sm);	\
    } while (0)

# define restoresigvec(sv, sm)			\
    do {					\
	sigaction(SIGINT,  &(sv)[0], NULL);	\
	sigaction(SIGQUIT, &(sv)[1], NULL);	\
	sigaction(SIGTSTP, &(sv)[2], NULL);	\
	sigaction(SIGTTIN, &(sv)[3], NULL);	\
	sigaction(SIGTTOU, &(sv)[4], NULL);	\
	sigaction(SIGTERM, &(sv)[5], NULL);	\
	sigaction(SIGHUP,  &(sv)[6], NULL);	\
	sigprocmask(SIG_SETMASK, &sm, NULL);	\
    } while (0)
# endif /* SAVESIGVEC */

extern int alrmcatch_disabled;
extern int pchild_disabled;
extern int phup_disabled;
extern int pintr_disabled;

extern void sigset_interrupting(int, void (*) (int));
extern int handle_pending_signals(void);

extern void queue_alrmcatch(int);
extern void queue_pchild(int);
extern void queue_phup(int);
extern void queue_pintr(int);

extern void disabled_cleanup(void *);
extern void pintr_disabled_restore(void *);
extern void pintr_push_enable(int *);

#endif /* _h_tc_sig */
