/* $Header: /p/tcsh/cvsroot/tcsh/ed.term.h,v 1.19 2015/03/25 19:53:16 christos Exp $ */
/*
 * ed.term.h: Local terminal header
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
#ifndef _h_ed_term
#define _h_ed_term

#define TO_CONTROL(A)	((A) & 037)

#if defined(TERMIO) || defined(POSIX)
/*
 * Aix compatible names
 */
# if defined(VWERSE) && !defined(VWERASE)
#  define VWERASE VWERSE
# endif /* VWERSE && !VWERASE */

# if defined(VDISCRD) && !defined(VDISCARD)
#  define VDISCARD VDISCRD
# endif /* VDISCRD && !VDISCARD */

# if defined(VFLUSHO) && !defined(VDISCARD)
#  define VDISCARD VFLUSHO
# endif  /* VFLUSHO && VDISCARD */

# if defined(VSTRT) && !defined(VSTART)
#  define VSTART VSTRT
# endif /* VSTRT && ! VSTART */

# if defined(VSTAT) && !defined(VSTATUS)
#  define VSTATUS VSTAT
# endif /* VSTAT && ! VSTATUS */

# ifndef ONLRET
#  define ONLRET 0
# endif /* ONLRET */

# ifndef TAB3
#  ifdef OXTABS
#   define TAB3 OXTABS
#  else
#   define TAB3 0
#  endif /* OXTABS */
# endif /* !TAB3 */

# if defined(OXTABS) && !defined(XTABS)
#  define XTABS OXTABS
# endif /* OXTABS && !XTABS */

# ifndef ONLCR
#  define ONLCR 0
# endif /* ONLCR */

# ifndef IEXTEN
#  define IEXTEN 0
# endif /* IEXTEN */

/*
 * emx garbage
 */
# ifndef IDEFAULT
#  define IDEFAULT 0
# endif /* IDEFAULT */

# ifndef IDELETE
#  define IDELETE 0
# endif /* IDELETE */

# ifndef ECHOCTL
#  define ECHOCTL 0
# endif /* ECHOCTL */

# ifndef PARENB
#  define PARENB 0
# endif /* PARENB */

# ifndef EXTPROC
#  define EXTPROC 0
# endif /* EXTPROC */

# ifndef FLUSHO
#  define FLUSHO  0
# endif /* FLUSHO */


# if defined(VDISABLE) && !defined(_POSIX_VDISABLE)
#  define _POSIX_VDISABLE VDISABLE
# endif /* VDISABLE && ! _POSIX_VDISABLE */

/*
 * Work around ISC's definition of IEXTEN which is
 * XCASE!
 */
# ifdef ISC
#  if defined(IEXTEN) && defined(XCASE)
#   if IEXTEN == XCASE
#    undef IEXTEN
#    define IEXTEN 0
#   endif /* IEXTEN == XCASE */
#  endif /* IEXTEN && XCASE */
#  if defined(IEXTEN) && !defined(XCASE)
#   define XCASE IEXTEN
#   undef IEXTEN
#   define IEXTEN 0
#  endif /* IEXTEN && !XCASE */
# endif /* ISC */

/*
 * Work around convex weirdness where turning off IEXTEN makes us
 * lose all postprocessing!
 */
#ifdef convex
# if defined(IEXTEN) && IEXTEN != 0
#  undef IEXTEN
#  define IEXTEN 0
# endif /* IEXTEN != 0 */
#endif /* convex */


# else /* SGTTY */

# ifndef LPASS8
#  define LPASS8  0
# endif /* LPASS8 */

#endif /* TERMIO || POSIX */

#ifndef _POSIX_VDISABLE
# define _POSIX_VDISABLE ((unsigned char) -1)
#endif /* _POSIX_VDISABLE */


#if !defined(CREPRINT) && defined(CRPRNT)
# define CREPRINT CRPRNT
#endif /* !CREPRINT && CRPRNT */
#if !defined(CDISCARD) && defined(CFLUSH)
# define CDISCARD CFLUSH
#endif /* !CDISCARD && CFLUSH */
#if !defined(CDISCARD) && defined(CFLUSHO)
# define CDISCARD CFLUSHO
#endif /* !CDISCARD && CFLUSHO */

/*
 * IRIX4.0 control macro is broken!
 * Ignore and undef all default tty chars defined and redefine only
 * the ones that are different in the IRIX file.
 */
#if __STDC__ && defined(IRIS4D)
# undef  CINTR
# define CINTR		0177	/* ^? */
# undef  CQUIT
# undef  CERASE
# define CERASE		TO_CONTROL('h')
# undef  CKILL
# undef  CEOF
# undef  CEOL
# undef  CEOL2	
# undef  CSWTCH	
# define CSWTCH		TO_CONTROL('z')
# undef  CDSWTCH 
# undef  CERASE2
# undef  CSTART
# undef  CSTOP
# undef  CWERASE
# undef  CSUSP
# undef  CDSUSP
# undef  CREPRINT
# undef  CDISCARD
# undef  CLNEXT
# undef  CSTATUS
# undef  CPAGE
# undef  CPGOFF
# undef  CKILL2
# undef  CBRK
# undef  CMIN
# undef  CTIME
#endif /* __STDC__ && IRIS4D */


#ifndef CINTR
# define CINTR		TO_CONTROL('c')
#endif /* CINTR */
#ifndef CQUIT
# define CQUIT		034	/* ^\ */
#endif /* CQUIT */
#ifndef CERASE
# define CERASE		0177	/* ^? */
#endif /* CERASE */
#ifndef CKILL
# define CKILL		TO_CONTROL('u')
#endif /* CKILL */
#ifndef CEOF
# define CEOF		TO_CONTROL('d')
#endif /* CEOF */
#ifndef CEOL
# define CEOL		_POSIX_VDISABLE
#endif /* CEOL */
#ifndef CEOL2
# define CEOL2		_POSIX_VDISABLE
#endif /* CEOL2 */
#ifndef CSWTCH
# define CSWTCH		_POSIX_VDISABLE
#endif /* CSWTCH */
#ifndef CDSWTCH
# define CDSWTCH	_POSIX_VDISABLE
#endif /* CDSWTCH */
#ifndef CERASE2
# define CERASE2	_POSIX_VDISABLE
#endif /* CERASE2 */
#ifndef CSTART
# define CSTART		TO_CONTROL('q')
#endif /* CSTART */
#ifndef CSTOP
# define CSTOP		TO_CONTROL('s')
#endif /* CSTOP */
#ifndef CSUSP
# define CSUSP		TO_CONTROL('z')
#endif /* CSUSP */
#ifndef CDSUSP
# define CDSUSP		TO_CONTROL('y')
#endif /* CDSUSP */

#ifdef hpux

# ifndef CREPRINT
#  define CREPRINT	_POSIX_VDISABLE
# endif /* CREPRINT */
# ifndef CDISCARD
#  define CDISCARD	_POSIX_VDISABLE
# endif /* CDISCARD */
# ifndef CLNEXT
#  define CLNEXT	_POSIX_VDISABLE
# endif /* CLNEXT */
# ifndef CWERASE
#  define CWERASE	_POSIX_VDISABLE
# endif /* CWERASE */

#else /* !hpux */

# ifndef CREPRINT
#  define CREPRINT	TO_CONTROL('r')
# endif /* CREPRINT */
# ifndef CDISCARD
#  define CDISCARD	TO_CONTROL('o')
# endif /* CDISCARD */
# ifndef CLNEXT
#  define CLNEXT	TO_CONTROL('v')
# endif /* CLNEXT */
# ifndef CWERASE
#  define CWERASE	TO_CONTROL('w')
# endif /* CWERASE */

#endif /* hpux */

#ifndef CSTATUS
# define CSTATUS	TO_CONTROL('t')
#endif /* CSTATUS */
#ifndef CPAGE
# define CPAGE		' '
#endif /* CPAGE */
#ifndef CPGOFF
# define CPGOFF		TO_CONTROL('m')
#endif /* CPGOFF */
#ifndef CKILL2
# define CKILL2		_POSIX_VDISABLE
#endif /* CKILL2 */
#ifndef CBRK
# ifndef masscomp
#  define CBRK		0377
# else
#  define CBRK		'\0'
# endif /* masscomp */
#endif /* CBRK */
#ifndef CMIN
# if VMIN == VEOF
#  define CMIN		CEOF
# else
#  define CMIN		1
# endif
#endif /* CMIN */
#ifndef CTIME
# if VTIME == VEOL
#  define CTIME		CEOL
# else
#  define CTIME		0
# endif
#endif /* CTIME */

/*
 * Fix for sun inconsistency. On termio VSUSP and the rest of the
 * ttychars > NCC are defined. So we undefine them.
 */
#if defined(TERMIO) || defined(POSIX)
# if defined(POSIX) && defined(NCCS)
#  define NUMCC		NCCS
# else
#  ifdef NCC
#   define NUMCC	NCC
#  endif /* NCC */
# endif /* POSIX && NCCS */
# ifdef NUMCC
#  ifdef VINTR
#   if NUMCC <= VINTR
#    undef VINTR
#   endif /* NUMCC <= VINTR */
#  endif /* VINTR */
#  ifdef VQUIT
#   if NUMCC <= VQUIT
#    undef VQUIT
#   endif /* NUMCC <= VQUIT */
#  endif /* VQUIT */
#  ifdef VERASE
#   if NUMCC <= VERASE
#    undef VERASE
#   endif /* NUMCC <= VERASE */
#  endif /* VERASE */
#  ifdef VKILL
#   if NUMCC <= VKILL
#    undef VKILL
#   endif /* NUMCC <= VKILL */
#  endif /* VKILL */
#  ifdef VEOF
#   if NUMCC <= VEOF
#    undef VEOF
#   endif /* NUMCC <= VEOF */
#  endif /* VEOF */
#  ifdef VEOL
#   if NUMCC <= VEOL
#    undef VEOL
#   endif /* NUMCC <= VEOL */
#  endif /* VEOL */
#  ifdef VEOL2
#   if NUMCC <= VEOL2
#    undef VEOL2
#   endif /* NUMCC <= VEOL2 */
#  endif /* VEOL2 */
#  ifdef VSWTCH
#   if NUMCC <= VSWTCH
#    undef VSWTCH
#   endif /* NUMCC <= VSWTCH */
#  endif /* VSWTCH */
#  ifdef VDSWTCH
#   if NUMCC <= VDSWTCH
#    undef VDSWTCH
#   endif /* NUMCC <= VDSWTCH */
#  endif /* VDSWTCH */
#  ifdef VERASE2
#   if NUMCC <= VERASE2
#    undef VERASE2
#   endif /* NUMCC <= VERASE2 */
#  endif /* VERASE2 */
#  ifdef VSTART
#   if NUMCC <= VSTART
#    undef VSTART
#   endif /* NUMCC <= VSTART */
#  endif /* VSTART */
#  ifdef VSTOP
#   if NUMCC <= VSTOP
#    undef VSTOP
#   endif /* NUMCC <= VSTOP */
#  endif /* VSTOP */
#  ifdef VWERASE
#   if NUMCC <= VWERASE
#    undef VWERASE
#   endif /* NUMCC <= VWERASE */
#  endif /* VWERASE */
#  ifdef VSUSP
#   if NUMCC <= VSUSP
#    undef VSUSP
#   endif /* NUMCC <= VSUSP */
#  endif /* VSUSP */
#  ifdef VDSUSP
#   if NUMCC <= VDSUSP
#    undef VDSUSP
#   endif /* NUMCC <= VDSUSP */
#  endif /* VDSUSP */
#  ifdef VREPRINT
#   if NUMCC <= VREPRINT
#    undef VREPRINT
#   endif /* NUMCC <= VREPRINT */
#  endif /* VREPRINT */
#  ifdef VDISCARD
#   if NUMCC <= VDISCARD
#    undef VDISCARD
#   endif /* NUMCC <= VDISCARD */
#  endif /* VDISCARD */
#  ifdef VLNEXT
#   if NUMCC <= VLNEXT
#    undef VLNEXT
#   endif /* NUMCC <= VLNEXT */
#  endif /* VLNEXT */
#  ifdef VSTATUS
#   if NUMCC <= VSTATUS
#    undef VSTATUS
#   endif /* NUMCC <= VSTATUS */
#  endif /* VSTATUS */
#  ifdef VPAGE
#   if NUMCC <= VPAGE
#    undef VPAGE
#   endif /* NUMCC <= VPAGE */
#  endif /* VPAGE */
#  ifdef VPGOFF
#   if NUMCC <= VPGOFF
#    undef VPGOFF
#   endif /* NUMCC <= VPGOFF */
#  endif /* VPGOFF */
#  ifdef VKILL2
#   if NUMCC <= VKILL2
#    undef VKILL2
#   endif /* NUMCC <= VKILL2 */
#  endif /* VKILL2 */
#  ifdef VBRK
#   if NUMCC <= VBRK
#    undef VBRK
#   endif /* NUMCC <= VBRK */
#  endif /* VBRK */
#  ifdef VMIN
#   if NUMCC <= VMIN
#    undef VMIN
#   endif /* NUMCC <= VMIN */
#  endif /* VMIN */
#  ifdef VTIME
#   if NUMCC <= VTIME
#    undef VTIME
#   endif /* NUMCC <= VTIME */
#  endif /* VTIME */
# endif /* NUMCC */
#endif /* !POSIX */

/*
 * fix for hpux10 inconsistency: it has VWERASE, but TIOCSLTC returns
 * EINVAL if one tries to change it
 * Also for RH6.2 on the alpha, defined TIOCGLTC, but does not have
 * struct ltchars
 */
#if (defined(hpux) && defined(VSUSP) && defined(VDSUSP) && defined(VWERASE) && defined(VLNEXT)) || defined(__linux__) || defined(__GNU__) || defined(__GLIBC__) || defined(__QNXNTO__)
# undef TIOCGLTC       /* not really needed */
# undef TIOCSLTC
#endif

#define C_INTR		 0
#define C_QUIT		 1
#define C_ERASE		 2
#define C_KILL		 3
#define C_EOF		 4
#define C_EOL		 5
#define C_EOL2		 6
#define C_SWTCH		 7
#define C_DSWTCH	 8
#define C_ERASE2	 9
#define C_START		10
#define C_STOP		11
#define C_WERASE	12
#define C_SUSP		13
#define C_DSUSP		14
#define C_REPRINT	15
#define C_DISCARD	16
#define C_LNEXT		17
#define C_STATUS	18
#define C_PAGE		19
#define C_PGOFF		20
#define C_KILL2		21
#define C_BRK		22
#define C_MIN		23
#define C_TIME		24
#define C_NCC		25
#define C_SH(A)		(1 << (A))

/*
 * Terminal dependend data structures
 */
typedef struct {
#ifdef WINNT_NATIVE
    int dummy;
#else /* !WINNT_NATIVE */
# if defined(POSIX) || defined(TERMIO)
#  ifdef POSIX
    struct termios d_t;
#  else
    struct termio d_t;
#  endif /* POSIX */
# else /* SGTTY */
#  ifdef TIOCGETP
    struct sgttyb d_t;
#  endif /* TIOCGETP */
#  ifdef TIOCGETC
    struct tchars d_tc;
#  endif /* TIOCGETC */
#  ifdef TIOCGPAGE
    struct ttypagestat d_pc;
#  endif /* TIOCGPAGE */
#  ifdef TIOCLGET
    int d_lb;
#  endif /* TIOCLGET */
# endif /* POSIX || TERMIO */
# ifdef TIOCGLTC
    struct ltchars d_ltc;
# endif /* TIOCGLTC */
#endif /* WINNT_NATIVE */
} ttydata_t;

#endif /* _h_ed_term */
