/* $Header: /p/tcsh/cvsroot/tcsh/sh.h,v 3.178 2016/09/12 16:33:54 christos Exp $ */
/*
 * sh.h: Catch it all globals and includes file!
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
#ifndef _h_sh
#define _h_sh

#include "config.h"

#include <stddef.h>
#include <signal.h>

#ifdef HAVE_ICONV
# include <iconv.h>
#endif

#ifdef HAVE_STDINT_H
# include <stdint.h>
#endif

#ifdef HAVE_INTTYPES_H
# include <inttypes.h>
#endif

#if !defined(HAVE_STDINT_H) && !defined(HAVE_INTTYPES_H) && !defined(WINNT_NATIVE)
typedef unsigned long intptr_t;
#endif

#ifndef EXTERN
# define EXTERN extern
#else /* !EXTERN */
# ifdef WINNT_NATIVE
#  define IZERO = 0
#  define IZERO_STRUCT = {0}
# endif /* WINNT_NATIVE */
#endif /* EXTERN */

#ifndef IZERO
# define IZERO
#endif /* IZERO */
#ifndef IZERO_STRUCT
# define IZERO_STRUCT
#endif /* IZERO_STRUCT */

#ifndef WINNT_NATIVE
# define INIT_ZERO
# define INIT_ZERO_STRUCT
# define force_read xread
#endif /*!WINNT_NATIVE */

#if defined(KANJI) && defined(WIDE_STRINGS) && defined(HAVE_NL_LANGINFO) && defined(CODESET)
#define AUTOSET_KANJI
#endif
/*
 * Sanity
 */
#if defined(_POSIX_SOURCE) && !defined(POSIX)
# define POSIX
#endif 

#if defined(POSIXJOBS) && !defined(BSDJOBS)
# define BSDJOBS
#endif 

#define TMP_TEMPLATE ".XXXXXX"

#ifdef SHORT_STRINGS
# ifdef WIDE_STRINGS
#include <wchar.h>
#  ifdef UTF16_STRINGS
typedef wint_t Char;
#  else
typedef wchar_t Char;
#endif
typedef unsigned long uChar;
typedef wint_t eChar; /* Can contain any Char value or CHAR_ERR */
#define CHAR_ERR WEOF /* Pretty please, use bit 31... */
#define normal_mbtowc(PWC, S, N) rt_mbtowc(PWC, S, N)
#define reset_mbtowc() TCSH_IGNORE(mbtowc(NULL, NULL, 0))
# else
typedef short Char;
typedef unsigned short uChar;
typedef int eChar;
#define CHAR_ERR (-1)
#define normal_mbtowc(PWC, S, N) ((void)(N), *(PWC) = (unsigned char)*(S), 1)
#define reset_mbtowc() ((void)0)
# endif
# define SAVE(a) (Strsave(str2short(a)))
#else
typedef char Char;
typedef unsigned char uChar;
typedef int eChar;
#define CHAR_ERR (-1)
#define normal_mbtowc(PWC, S, N) ((void)(N), *(PWC) = (unsigned char)*(S), 1)
#define reset_mbtowc() ((void)0)
# define SAVE(a) (strsave(a))
#endif

#if !defined(__inline) && !defined(__GNUC__) && !defined(_MSC_VER)
#define __inline
#endif
#ifdef _MSC_VER
#define TCSH_PTRDIFF_T_FMT "I"
#else
#define TCSH_PTRDIFF_T_FMT "t"
#endif
/* Elide unused argument warnings */
#define USE(a)	(void) (a)
#define TCSH_IGNORE(a)	tcsh_ignore((intptr_t)a)
static __inline void tcsh_ignore(intptr_t a)
{
    USE(a);
}

/*
 * Return true if the path is absolute
 */
#if defined(WINNT_NATIVE)
# define ABSOLUTEP(p)	((p)[0] == '/' || \
    (Isalpha((p)[0]) && (p)[1] == ':'))
#elif defined(__CYGWIN__)
# define ABSOLUTEP(p)	((p)[0] == '/' || \
    (Isalpha((p)[0]) && (p)[1] == ':' && \
     ((p)[2] == '\0' || (p)[2] == '/')))
#else /* !WINNT_NATIVE && !__CYGWIN__ */
# define ABSOLUTEP(p)	(*(p) == '/')
#endif /* WINNT_NATIVE || __CYGWIN__ */

/*
 * Fundamental definitions which may vary from system to system.
 *
 *	BUFSIZE		The i/o buffering size; also limits word size
 *	MAILINTVL	How often to mailcheck; more often is more expensive
 */
#ifdef BUFSIZE
# if	   BUFSIZE < 4096
#  undef   BUFSIZE
#  define  BUFSIZE	4096	/* buffer size should be no less than this */
# endif
#else
# define   BUFSIZE	4096
#endif /* BUFSIZE */

#define FORKSLEEP	10	/* delay loop on non-interactive fork failure */
#define	MAILINTVL	600	/* 10 minutes */

#ifndef INBUFSIZE
# define INBUFSIZE    2*BUFSIZE /* Num input characters on the command line */
#endif /* INBUFSIZE */


/*
 * What our builtin echo looks like
 */
#define NONE_ECHO	0
#define BSD_ECHO	1
#define SYSV_ECHO	2
#define BOTH_ECHO	(BSD_ECHO|SYSV_ECHO)

#ifndef ECHO_STYLE
# if SYSVREL > 0
#  define ECHO_STYLE SYSV_ECHO
# else /* SYSVREL == 0 */
#  define ECHO_STYLE BSD_ECHO
# endif /* SYSVREL */
#endif /* ECHO_STYLE */

/* values for noclobber */
#define NOCLOBBER_DEFAULT  1
#define NOCLOBBER_NOTEMPTY 2
#define NOCLOBBER_ASK      4

/*
 * The shell moves std in/out/diag and the old std input away from units
 * 0, 1, and 2 so that it is easy to set up these standards for invoked
 * commands.
 */
#define	FSAFE	5		/* We keep the first 5 descriptors untouched */
#define	FSHTTY	15		/* /dev/tty when manip pgrps */
#define	FSHIN	16		/* Preferred desc for shell input */
#define	FSHOUT	17		/* ... shell output */
#define	FSHDIAG	18		/* ... shell diagnostics */
#define	FOLDSTD	19		/* ... old std input */

#ifdef PROF
#define	xexit(n)	done(n)
#endif 

#ifdef cray
# define word word_t           /* sys/types.h defines word.. bad move! */
#endif

#include <sys/types.h>

#ifdef cray
# undef word
#endif 

/* 
 * Path separator in environment variables
 */
#ifndef PATHSEP
# if defined(__EMX__) || defined(WINNT_NATIVE)
#  define PATHSEP ';'
# else /* unix */
#  define PATHSEP ':'
# endif /* __EMX__ || WINNT_NATIVE */
#endif /* !PATHSEP */

#if defined(__HP_CXD_SPP) && !defined(__hpux)
# include <sys/cnx_stat.h>
# define stat stat64
# define fstat fstat64
# define lstat lstat64
#endif /* __HP_CXD_SPP && !__hpux */

#ifdef HAVE_LONG_LONG
typedef long long tcsh_number_t;
#else
typedef long tcsh_number_t;
#endif
/*
 * This macro compares the st_dev field of struct stat. On aix on ibmESA
 * st_dev is a structure, so comparison does not work. 
 */
#ifndef DEV_DEV_COMPARE
# define DEV_DEV_COMPARE(x,y)   ((x) == (y))
#endif /* DEV_DEV_COMPARE */

#ifdef _SEQUENT_
# include <sys/procstats.h>
#endif /* _SEQUENT_ */
#if (defined(POSIX) || SYSVREL > 0) && !defined(WINNT_NATIVE)
# include <sys/times.h>
#endif /* (POSIX || SYSVREL > 0) && !WINNT_NATIVE */

#ifdef NLS
# include <locale.h>
#endif /* NLS */

#if !defined(_MINIX) && !defined(_VMS_POSIX) && !defined(WINNT_NATIVE) && !defined(__MVS__)
# include <sys/param.h>
#endif /* !_MINIX && !_VMS_POSIX && !WINNT_NATIVE && !__MVS__ */
#include <sys/stat.h>

#if defined(BSDTIMES) || defined(BSDLIMIT)
# include <sys/time.h>
# if SYSVREL>3 && !defined(SCO) && !defined(sgi) && !defined(SNI) && !defined(sun) && !(defined(__alpha) && defined(__osf__)) && !defined(_SX) && !defined(__MVS__)
#  include "/usr/ucbinclude/sys/resource.h"
# else
#  ifdef convex
#   define sysrusage cvxrusage
#   include <sys/sysinfo.h>
#  else
#   define sysrusage rusage
#   include <sys/resource.h>
#  endif /* convex */
# endif /* SYSVREL>3 */
#endif /* BSDTIMES */

#ifndef WINNT_NATIVE
# ifndef POSIX
#  ifdef TERMIO
#   include <termio.h>
#  else /* SGTTY */
#   include <sgtty.h>
#  endif /* TERMIO */
# else /* POSIX */
#  ifndef _UWIN
#   include <termios.h>
#  else
#   include <termio.h>
#  endif /* _UWIN */
#  if SYSVREL > 3 || defined(__linux__)
#   undef TIOCGLTC	/* we don't need those, since POSIX has them */
#   undef TIOCSLTC
#   undef CSWTCH
#   define CSWTCH _POSIX_VDISABLE	/* So job control works */
#  endif /* SYSVREL > 3 */
# endif /* POSIX */
#endif /* WINNT_NATIVE */

#ifdef sonyrisc
# include <sys/ttold.h>
#endif /* sonyrisc */

#if defined(POSIX) && !defined(WINNT_NATIVE)
# include <unistd.h>

/*
 * the gcc+protoize version of <stdlib.h>
 * redefines malloc(), so we define the following
 * to avoid it.
 */
# if defined(SYSMALLOC) || defined(__linux__) || defined(__GNU__) || defined(__GLIBC__) || defined(sgi) || defined(_OSD_POSIX)
#  define NO_FIX_MALLOC
#  include <stdlib.h>
# else /* glibc */
#  define _GNU_STDLIB_H
#  define malloc __malloc
#  define free __free
#  define calloc __calloc
#  define realloc __realloc
#  include <stdlib.h>
#  undef malloc
#  undef free
#  undef calloc
#  undef realloc
# endif /* glibc || sgi */
#endif /* POSIX && !WINNT_NATIVE */
#include <limits.h>

#if SYSVREL > 0 || defined(_IBMR2) || defined(_MINIX) || defined(__linux__) || defined(__GNU__) || defined(__GLIBC__)
# if !defined(pyr) && !defined(stellar)
#  include <time.h>
#  ifdef _MINIX
#   define HZ CLOCKS_PER_SEC
#  endif /* _MINIX */
# endif /* !pyr && !stellar */
#endif /* SYSVREL > 0 ||  _IBMR2 */

/* In the following ifdef the DECOSF1 has been commented so that later
 * versions of DECOSF1 will get TIOCGWINSZ. This might break older versions...
 */
#if !((defined(SUNOS4) || defined(_MINIX) /* || defined(DECOSF1) */) && defined(TERMIO))
# if !defined(_VMS_POSIX) && !defined(WINNT_NATIVE)
#  include <sys/ioctl.h>
#  if SYSVREL > 3 || defined(__linux__)
#   undef TIOCGLTC	/* we don't need those, since POSIX has them */
#   undef TIOCSLTC
#   undef CSWTCH
#   define CSWTCH _POSIX_VDISABLE	/* So job control works */
#  endif /* SYSVREL > 3 */
# endif
#endif 

#if (defined(__DGUX__) && defined(POSIX)) || defined(DGUX)
#undef CSWTCH
#define CSWTCH _POSIX_VDISABLE
#endif

#if (!defined(FIOCLEX) && defined(SUNOS4)) || ((SYSVREL == 4) && !defined(_SEQUENT_) && !defined(SCO) && !defined(_SX)) && !defined(__MVS__)
# include <sys/filio.h>
#endif /* (!FIOCLEX && SUNOS4) || (SYSVREL == 4 && !_SEQUENT_ && !SCO && !_SX ) */

#if !defined(_MINIX) && !defined(supermax) && !defined(WINNT_NATIVE) && !defined(IRIS4D)
# include <sys/file.h>
#endif	/* !_MINIX && !supermax && !WINNT_NATIVE && !defined(IRIS4D) */

#if !defined(O_RDONLY) || !defined(O_NDELAY)
# include <fcntl.h>
#endif 

#include <errno.h>

#include <setjmp.h>

#include <stdarg.h>

#ifdef HAVE_DIRENT_H
# include <dirent.h>
#else
# ifdef HAVE_NDIR_H
#  include <ndir.h>
# else
#  include <sys/dir.h>
# endif
# define dirent direct
#endif /* HAVE_DIRENT_H */
#ifndef HAVE_STRUCT_DIRENT_D_INO
# define d_ino d_fileno
#endif
#if defined(hpux) || defined(sgi) || defined(OREO)
# include <stdio.h>	/* So the fgetpwent() prototypes work */
#endif /* hpux || sgi || OREO */
#ifndef WINNT_NATIVE
#include <pwd.h>
#include <grp.h>
#endif /* WINNT_NATIVE */
#ifdef HAVE_SHADOW_H
# include <shadow.h>
#endif /* HAVE_SHADOW_H */
#ifdef HAVE_AUTH_H
# include <auth.h>
#endif /* HAVE_AUTH_H */
#if defined(BSD) && !defined(POSIX)
# include <strings.h>
# define strchr(a, b) index(a, b)
# define strrchr(a, b) rindex(a, b)
#else
# include <string.h>
#endif /* BSD */

/*
 * IRIX-5.0 has <sys/cdefs.h>, but most system include files do not
 * include it yet, so we include it here
 */
#if defined(sgi) && SYSVREL > 3
# include <sys/cdefs.h>
#endif /* sgi && SYSVREL > 3 */

#ifdef REMOTEHOST
# ifdef ISC
#  undef MAXHOSTNAMELEN	/* Busted headers? */
# endif

# include <netinet/in.h>
# include <arpa/inet.h>
# include <sys/socket.h>
# if (defined(_SS_SIZE) || defined(_SS_MAXSIZE)) && defined(HAVE_STRUCT_SOCKADDR_STORAGE_SS_FAMILY)
#  if !defined(__APPLE__) /* Damnit, where is getnameinfo() folks? */
#   if !defined(sgi)
#    define INET6
#   endif /* sgi */
#  endif /* __APPLE__ */
# endif
# include <sys/uio.h>	/* For struct iovec */
#endif /* REMOTEHOST */

#ifdef PURIFY
/* exit normally, allowing purify to trace leaks */
# define _exit		exit
#endif /* !PURIFY */

/*
 * ASCII vs. EBCDIC
 */
#if 'Z' - 'A' == 25
# ifndef IS_ASCII
#  define IS_ASCII
# endif
#endif

#include "sh.types.h"

#if !HAVE_DECL_GETPGRP
# ifndef GETPGRP_VOID
extern pid_t getpgrp (int);
# else
extern pid_t getpgrp (void);
# endif
#endif

#ifndef lint
typedef ptr_t memalign_t;
#else
typedef union {
    char    am_char, *am_char_p;
    short   am_short, *am_short_p;
    int     am_int, *am_int_p;
    long    am_long, *am_long_p;
    float   am_float, *am_float_p;
    double  am_double, *am_double_p;
}      *memalign_t;

# define malloc		lint_malloc
# define free		lint_free
# define realloc	lint_realloc
# define calloc		lint_calloc
#endif 

#ifdef SYSMALLOC
# define xmalloc(i)	smalloc(i)
# define xrealloc(p, i)	srealloc(p, i)
# define xcalloc(n, s)	scalloc(n, s)
# define xfree		sfree
#else
# define xmalloc(i)  	malloc(i)
# define xrealloc(p, i)	realloc(p, i)
# define xcalloc(n, s)	calloc(n, s)
# define xfree	 	free
#endif /* SYSMALLOC */
#include "sh.char.h"
#include "sh.err.h"
#include "sh.dir.h"
#include "sh.proc.h"

#include "pathnames.h"


/*
 * C shell
 *
 * Bill Joy, UC Berkeley
 * October, 1978; May 1980
 *
 * Jim Kulp, IIASA, Laxenburg Austria
 * April, 1980
 */

#ifdef HESIOD
# include <hesiod.h>
#endif /* HESIOD */

#ifdef REMOTEHOST
# include <netdb.h>
#endif /* REMOTEHOST */

#ifndef MAXHOSTNAMELEN
# ifdef HOST_NAME_MAX
#  define MAXHOSTNAMELEN (HOST_NAME_MAX + 1)
# elif defined(SCO) && (SYSVREL > 3)
#  include <sys/socket.h>
# else
#  define MAXHOSTNAMELEN 256
# endif
#endif /* MAXHOSTNAMELEN */



#define	eq(a, b)	(Strcmp(a, b) == 0)

/* globone() flags */
#define G_ERROR		0	/* default action: error if multiple words */
#define G_IGNORE	1	/* ignore the rest of the words		   */
#define G_APPEND	2	/* make a sentence by cat'ing the words    */

/*
 * Global flags
 */
EXTERN int    chkstop IZERO;	/* Warned of stopped jobs... allow exit */

#if (defined(FIOCLEX) && defined(FIONCLEX)) || defined(F_SETFD)
# define CLOSE_ON_EXEC
#else
EXTERN int    didcch IZERO;	/* Have closed unused fd's for child */
#endif /* (FIOCLEX && FIONCLEX) || F_SETFD */

EXTERN int    didfds IZERO;	/* Have setup i/o fd's for child */
EXTERN int    doneinp IZERO;	/* EOF indicator after reset from readc */
EXTERN int    exiterr IZERO;	/* Exit if error or non-zero exit status */
EXTERN int    child IZERO;	/* Child shell ... errors cause exit */
EXTERN int    haderr IZERO;	/* Reset was because of an error */
EXTERN int    intty IZERO;	/* Input is a tty */
EXTERN int    intact IZERO;	/* We are interactive... therefore prompt */
EXTERN int    justpr IZERO;	/* Just print because of :p hist mod */
EXTERN int    loginsh IZERO;	/* We are a loginsh -> .login/.logout */
EXTERN int    neednote IZERO;	/* Need to pnotify() */
EXTERN int    noexec IZERO;	/* Don't execute, just syntax check */
EXTERN int    pjobs IZERO;	/* want to print jobs if interrupted */
EXTERN int    setintr IZERO;	/* Set interrupts on/off -> Wait intr... */
EXTERN int    handle_interrupt IZERO;/* Are we currently handling an interrupt? */
EXTERN int    havhash IZERO;	/* path hashing is available */
EXTERN int    editing IZERO;	/* doing filename expansion and line editing */
EXTERN int    noediting IZERO;	/* initial $term defaulted to noedit */
EXTERN int    bslash_quote IZERO;/* PWP: tcsh-style quoting?  (in sh.c) */
EXTERN int    anyerror IZERO;	/* propagate errors from pipelines/backq */
EXTERN int    compat_expr IZERO;/* csh-style expressions? */
EXTERN int    isoutatty IZERO;	/* is SHOUT a tty */
EXTERN int    isdiagatty IZERO;/* is SHDIAG a tty */
EXTERN int    is1atty IZERO;	/* is file descriptor 1 a tty (didfds mode) */
EXTERN int    is2atty IZERO;	/* is file descriptor 2 a tty (didfds mode) */
EXTERN int    arun IZERO;	/* Currently running multi-line-aliases */
EXTERN int    implicit_cd IZERO;/* implicit cd enabled?(1=enabled,2=verbose) */
EXTERN int    cdtohome IZERO;	/* cd without args goes home */
EXTERN int    inheredoc IZERO;	/* Currently parsing a heredoc */
EXTERN int    no_clobber IZERO;	/* no clobber enabled? 1=yes 2=notempty, 4=ask*/
/* We received a window change event */
EXTERN volatile sig_atomic_t windowchg IZERO;
#if defined(KANJI) && defined(SHORT_STRINGS) && defined(DSPMBYTE)
EXTERN int    dspmbyte_ls;
#endif

/*
 * Global i/o info
 */
EXTERN Char   *arginp IZERO;	/* Argument input for sh -c and internal `xx` */
EXTERN int     onelflg IZERO;	/* 2 -> need line for -t, 1 -> exit on read */
extern Char   *ffile;		/* Name of shell file for $0 */
extern int    dolzero;		/* if $?0 should return true... */

extern char *seterr;		/* Error message from scanner/parser */
#ifndef errno
extern int errno;		/* Error from C library routines */
#endif
extern int exitset;
/* Temp name for << shell files in /tmp, for xfree() */
EXTERN Char   *shtemp IZERO;

#ifdef BSDTIMES
EXTERN struct timeval time0;	/* Time at which the shell started */
EXTERN struct sysrusage ru0;
#else
# ifdef _SEQUENT_
EXTERN timeval_t time0;		/* time at which shell started */
EXTERN struct process_stats ru0;
# else /* _SEQUENT_ */
#  ifndef POSIX
EXTERN time_t  time0;		/* time at which shell started */
#  else	/* POSIX */
EXTERN clock_t time0;		/* time at which shell started */
EXTERN clock_t clk_tck;
#  endif /* POSIX */
EXTERN struct tms shtimes;	/* shell and child times for process timing */
# endif /* _SEQUENT_ */
EXTERN time_t seconds0;
#endif /* BSDTIMES */

#ifndef HZ
# define HZ	100		/* for division into seconds */
#endif

/*
 * Miscellany
 */
EXTERN pid_t   mainpid;		/* pid of the main shell ($$) */
EXTERN Char   *doldol;		/* Character pid for $$ */
EXTERN pid_t   backpid;		/* pid of the last background job */


/*
 * Ideally these should be uid_t, gid_t, pid_t. I cannot do that right now
 * cause pid's could be unsigned and that would break our -1 flag, and 
 * uid_t and gid_t are not defined in all the systems so I would have to
 * make special cases for them. In the future...
 */
EXTERN uid_t   uid, euid; 	/* Invokers real and effective */
EXTERN gid_t   gid, egid;	/* User and group ids */
EXTERN pid_t   opgrp,		/* Initial pgrp and tty pgrp */
               shpgrp,		/* Pgrp of shell */
               tpgrp;		/* Terminal process group */
				/* If tpgrp is -1, leave tty alone! */

EXTERN Char   *Prompt;		/* The actual printed prompt or NULL */
EXTERN Char   *RPrompt;		/* Right-hand side prompt or NULL */

/*
 * To be able to redirect i/o for builtins easily, the shell moves the i/o
 * descriptors it uses away from 0,1,2.
 * Ideally these should be in units which are closed across exec's
 * (this saves work) but for version 6, this is not usually possible.
 * The desired initial values for these descriptors are defined in
 * sh.local.h.
 */
EXTERN int   SHIN IZERO;	/* Current shell input (script) */
EXTERN int   SHOUT IZERO;	/* Shell output */
EXTERN int   SHDIAG IZERO;	/* Diagnostic output... shell errs go here */
EXTERN int   OLDSTD IZERO;	/* Old standard input (def for cmds) */


#if (SYSVREL == 4 && defined(_UTS)) || defined(__linux__)
/* 
 * From: fadden@uts.amdahl.com (Andy McFadden)
 * we need sigsetjmp for UTS4, but not UTS2.1
 */
# define SIGSETJMP
#endif

/*
 * Error control
 *
 * Errors in scanning and parsing set up an error message to be printed
 * at the end and complete.  Other errors always cause a reset.
 * Because of source commands and .cshrc we need nested error catches.
 */

#ifdef SIGSETJMP
   typedef struct { sigjmp_buf j; } jmp_buf_t;
# define setexit()  sigsetjmp(reslab.j, 1)
# define _reset()    siglongjmp(reslab.j, 1)
#else
   typedef struct { jmp_buf j; } jmp_buf_t;
# define setexit()  setjmp(reslab.j)
# define _reset()    longjmp(reslab.j, 1)
#endif

#define getexit(a) (void) ((a) = reslab)
#define resexit(a) (void) (reslab = (a))

#define cpybin(a, b) (void) ((a) = (b))

extern jmp_buf_t reslab;

EXTERN Char   *gointr;		/* Label for an onintr transfer */

extern struct sigaction parintr;	/* Parents interrupt catch */
extern struct sigaction parterm;	/* Parents terminate catch */

/*
 * Lexical definitions.
 *
 * All lexical space is allocated dynamically.
 * The eighth/sixteenth bit of characters is used to prevent recognition,
 * and eventually stripped.
 */
#define		META		0200
#define		ASCII		0177
#ifdef WIDE_STRINGS		/* Implies SHORT_STRINGS */
/* 31st char bit used for 'ing (not 32nd, we want all values nonnegative) */
/*
 * Notice
 *
 * By fix for handling unicode name file, 32nd bit is used.
 * We need use '&' instead of '> or <' when comparing with INVALID_BYTE etc..
 * Cast to uChar is not recommended,
 *  becase Char is 4bytes but uChar is 8bytes on I32LP64. */
# define	QUOTE		0x80000000
# define	TRIM		0x7FFFFFFF /* Mask to strip quote bit */
# define	UNDER		0x08000000 /* Underline flag */
# define	BOLD		0x04000000 /* Bold flag */
# define	STANDOUT	0x02000000 /* Standout flag */
# define	LITERAL		0x01000000 /* Literal character flag */
# define	ATTRIBUTES	0x0F000000 /* The bits used for attributes */
# define	INVALID_BYTE	0xF0000000 /* Invalid character on input */
# ifdef SOLARIS2
#  define	CHAR		0x30FFFFFF /* Mask to mask out the character */
# else
#  define	CHAR		0x00FFFFFF /* Mask to mask out the character */
# endif
#elif defined (SHORT_STRINGS)
# define	QUOTE 	((Char)	0100000)/* 16nth char bit used for 'ing */
# define	TRIM		0073777	/* Mask to strip quote/lit bit */
# define	UNDER		0040000	/* Underline flag */
# define	BOLD		0020000	/* Bold flag */
# define	STANDOUT	0010000	/* Standout flag */
# define	LITERAL		0004000	/* Literal character flag */
# define	ATTRIBUTES	0074000	/* The bits used for attributes */
# define	INVALID_BYTE	0
# define	CHAR		0000377	/* Mask to mask out the character */
#else
# define	QUOTE 	((Char)	0200)	/* Eighth char bit used for 'ing */
# define	TRIM		0177	/* Mask to strip quote bit */
# define	UNDER		0000000	/* No extra bits to do both */
# define	BOLD		0000000	/* Bold flag */
# define	STANDOUT	META	/* Standout flag */
# define	LITERAL		0000000	/* Literal character flag */
# define	ATTRIBUTES	0200	/* The bits used for attributes */
# define	INVALID_BYTE	0
# define	CHAR		0000177	/* Mask to mask out the character */
#endif
#define		CHAR_DBWIDTH	(LITERAL|(LITERAL-1))

# define 	MAX_UTF32	0x7FFFFFFF	/* max UTF32 is U+7FFFFFFF */

EXTERN int     AsciiOnly;	/* If set only 7 bits expected in characters */

/*
 * Each level of input has a buffered input structure.
 * There are one or more blocks of buffered input for each level,
 * exactly one if the input is seekable and tell is available.
 * In other cases, the shell buffers enough blocks to keep all loops
 * in the buffer.
 *
 * If (WIDE_STRINGS && cantell), fbobp is always a byte offset, but
 * (fseekp - fbobp) and (feobp - fbobp) are character offsets (usable for
 * fbuf indexing).
 *
 * If (!cantell), all offsets are character offsets; if (!WIDE_STRINGS), there
 * is no difference between byte and character offsets.
 */
EXTERN struct Bin {
    off_t   Bfseekp;		/* Seek pointer, generally != lseek() value */
    off_t   Bfbobp;		/* Seekp of beginning of buffers */
    off_t   Bfeobp;		/* Seekp of end of buffers */
    int     Bfblocks;		/* Number of buffer blocks */
    Char  **Bfbuf;		/* The array of buffer blocks */
#ifdef WIDE_STRINGS
    /* Number of bytes in each character if (cantell) */
    unsigned char Bfclens[BUFSIZE + 1];
#endif
}       B;

/*
 * This structure allows us to seek inside aliases
 */
struct Ain {
    int type;
#define TCSH_I_SEEK 	 0		/* Invalid seek */
#define TCSH_A_SEEK	 1		/* Alias seek */
#define TCSH_F_SEEK	 2		/* File seek */
#define TCSH_E_SEEK	 3		/* Eval seek */
    union {
	off_t _f_seek;		/* A byte offset if (cantell) */
	Char* _c_seek;
    } fc;
#define f_seek fc._f_seek
#define c_seek fc._c_seek
    Char **a_seek;
} ;

extern int aret;		/* Type of last char returned */
#define SEEKEQ(a, b) ((a)->type == (b)->type && \
		      (a)->f_seek == (b)->f_seek && \
		      (a)->a_seek == (b)->a_seek)

#define	fseekp	B.Bfseekp
#define	fbobp	B.Bfbobp
#define	feobp	B.Bfeobp
#define	fblocks	B.Bfblocks
#define	fbuf	B.Bfbuf
#define fclens  B.Bfclens

/*
 * The shell finds commands in loops by reseeking the input
 * For whiles, in particular, it reseeks to the beginning of the
 * line the while was on; hence the while placement restrictions.
 */
EXTERN struct Ain lineloc;

EXTERN int    cantell;		/* Is current source tellable ? */

/*
 * Input lines are parsed into doubly linked circular
 * lists of words of the following form.
 */
struct wordent {
    Char   *word;
    struct wordent *prev;
    struct wordent *next;
};

/*
 * During word building, both in the initial lexical phase and
 * when expanding $ variable substitutions, expansion by `!' and `$'
 * must be inhibited when reading ahead in routines which are themselves
 * processing `!' and `$' expansion or after characters such as `\' or in
 * quotations.  The following flags are passed to the getC routines
 * telling them which of these substitutions are appropriate for the
 * next character to be returned.
 */
#define	DODOL	1
#define	DOEXCL	2
#define	DOALL	DODOL|DOEXCL

/*
 * Labuf implements a general buffer for lookahead during lexical operations.
 * Text which is to be placed in the input stream can be stuck here.
 * We stick parsed ahead $ constructs during initial input,
 * process id's from `$$', and modified variable values (from qualifiers
 * during expansion in sh.dol.c) here.
 */
extern struct Strbuf labuf;
EXTERN size_t lap; /* N/A if == labuf.len, index into labuf.s otherwise */

/*
 * Parser structure
 *
 * Each command is parsed to a tree of command structures and
 * flags are set bottom up during this process, to be propagated down
 * as needed during the semantics/exeuction pass (sh.sem.c).
 */
struct command {
    unsigned char   t_dtyp;	/* Type of node 		 */
#define	NODE_COMMAND	1	/* t_dcom <t_dlef >t_drit	 */
#define	NODE_PAREN	2	/* ( t_dspr ) <t_dlef >t_drit	 */
#define	NODE_PIPE	3	/* t_dlef | t_drit		 */
#define	NODE_LIST	4	/* t_dlef ; t_drit		 */
#define	NODE_OR		5	/* t_dlef || t_drit		 */
#define	NODE_AND	6	/* t_dlef && t_drit		 */
    unsigned char   t_nice;	/* Nice value			 */
#ifdef apollo
    unsigned char   t_systype;	/* System environment		 */
#endif 
    unsigned long   t_dflg;	/* Flags, e.g. F_AMPERSAND|... 	 */
/* save these when re-doing 	 */
#ifndef apollo
#define	F_SAVE	(F_NICE|F_TIME|F_NOHUP|F_HUP)	
#else
#define	F_SAVE	(F_NICE|F_TIME|F_NOHUP||F_HUP|F_VER)
#endif 
#define	F_AMPERSAND	(1<<0)	/* executes in background	 */
#define	F_APPEND	(1<<1)	/* output is redirected >>	 */
#define	F_PIPEIN	(1<<2)	/* input is a pipe		 */
#define	F_PIPEOUT	(1<<3)	/* output is a pipe		 */
#define	F_NOFORK	(1<<4)	/* don't fork, last ()ized cmd	 */
#define	F_NOINTERRUPT	(1<<5)	/* should be immune from intr's */
/* spare */
#define	F_STDERR	(1<<7)	/* redirect unit 2 with unit 1	 */
#define	F_OVERWRITE	(1<<8)	/* output was !			 */
#define	F_READ		(1<<9)	/* input redirection is <<	 */
#define	F_REPEAT	(1<<10)	/* reexec aft if, repeat,...	 */
#define	F_NICE		(1<<11)	/* t_nice is meaningful 	 */
#define	F_NOHUP		(1<<12)	/* nohup this command 		 */
#define	F_TIME		(1<<13)	/* time this command 		 */
#define F_BACKQ		(1<<14)	/* command is in ``		 */
#define F_HUP		(1<<15)	/* hup this command		 */
#ifdef apollo
#define F_VER		(1<<16)	/* execute command under SYSTYPE */
#endif 
    union {
	Char   *T_dlef;		/* Input redirect word 		 */
	struct command *T_dcar;	/* Left part of list/pipe 	 */
    }       L;
    union {
	Char   *T_drit;		/* Output redirect word 	 */
	struct command *T_dcdr;	/* Right part of list/pipe 	 */
    }       R;
#define	t_dlef	L.T_dlef
#define	t_dcar	L.T_dcar
#define	t_drit	R.T_drit
#define	t_dcdr	R.T_dcdr
    Char  **t_dcom;		/* Command/argument vector 	 */
    struct command *t_dspr;	/* Pointer to ()'d subtree 	 */
};


/*
 * The keywords for the parser
 */
#define	TC_BREAK	0
#define	TC_BRKSW	1
#define	TC_CASE		2
#define	TC_DEFAULT 	3
#define	TC_ELSE		4
#define	TC_END		5
#define	TC_ENDIF	6
#define	TC_ENDSW	7
#define	TC_EXIT		8
#define	TC_FOREACH	9
#define	TC_GOTO		10
#define	TC_IF		11
#define	TC_LABEL	12
#define	TC_LET		13
#define	TC_SET		14
#define	TC_SWITCH	15
#define	TC_TEST		16
#define	TC_THEN		17
#define	TC_WHILE	18

/*
 * These are declared here because they want to be
 * initialized in sh.init.c (to allow them to be made readonly)
 */

#if defined(hpux) && defined(__STDC__) && !defined(__GNUC__)
    /* Avoid hpux ansi mode spurious warnings */
typedef void (*bfunc_t) ();
#else
typedef void (*bfunc_t) (Char **, struct command *);
#endif /* hpux && __STDC__ && !__GNUC__ */

extern const struct biltins {
    const char   *bname;
    bfunc_t bfunct;
    int     minargs, maxargs;
} bfunc[];
extern int nbfunc;
#ifdef WINNT_NATIVE
extern struct biltins  nt_bfunc[];
extern int nt_nbfunc;
#endif /* WINNT_NATIVE*/
extern int bequiet;

extern struct srch {
    const char *s_name;
    int  s_value;
} srchn[];
extern int nsrchn;

/*
 * Structure defining the existing while/foreach loops at this
 * source level.  Loops are implemented by seeking back in the
 * input.  For foreach (fe), the word list is attached here.
 */
EXTERN struct whyle {
    struct Ain   w_start;	/* Point to restart loop */
    struct Ain   w_end;		/* End of loop (0 if unknown) */
    Char  **w_fe, **w_fe0;	/* Current/initial wordlist for fe */
    Char   *w_fename;		/* Name for fe */
    struct whyle *w_next;	/* Next (more outer) loop */
}      *whyles;

/*
 * Variable structure
 *
 * Aliases and variables are stored in AVL balanced binary trees.
 */
EXTERN struct varent {
    Char  **vec;		/* Array of words which is the value */
    Char   *v_name;		/* Name of variable/alias */
    int	    v_flags;		/* Flags */
#define VAR_ALL		-1
#define VAR_READONLY	1
#define VAR_READWRITE	2
#define VAR_NOGLOB	4
#define VAR_FIRST       32
#define VAR_LAST        64
    struct varent *v_link[3];	/* The links, see below */
    int     v_bal;		/* Balance factor */
}       shvhed IZERO_STRUCT, aliases IZERO_STRUCT;

#define v_left		v_link[0]
#define v_right		v_link[1]
#define v_parent	v_link[2]

#define adrof(v)	adrof1(v, &shvhed)
#define varval(v)	value1(v, &shvhed)

/*
 * The following are for interfacing redo substitution in
 * aliases to the lexical routines.
 */
EXTERN struct wordent *alhistp IZERO_STRUCT;/* Argument list (first) */
EXTERN struct wordent *alhistt IZERO_STRUCT;/* Node after last in arg list */
EXTERN Char  **alvec IZERO_STRUCT,
	      *alvecp IZERO_STRUCT;/* The (remnants of) alias vector */

/*
 * Filename/command name expansion variables
 */

#ifndef MAXPATHLEN
# ifdef PATH_MAX
#  define MAXPATHLEN PATH_MAX
# else
#  define MAXPATHLEN 2048
# endif
#endif /* MAXPATHLEN */

#ifndef HAVENOLIMIT
/*
 * resource limits
 */
extern struct limits {
    int         limconst;
    const char *limname;
    int         limdiv;
    const char *limscale;
} limits[];
#endif /* !HAVENOLIMIT */

/*
 * History list
 *
 * Each history list entry contains an embedded wordlist
 * from the scanner, a number for the event, and a reference count
 * to aid in discarding old entries.
 *
 * Essentially "invisible" entries are put on the history list
 * when history substitution includes modifiers, and thrown away
 * at the next discarding since their event numbers are very negative.
 */
EXTERN struct Hist {
    struct wordent Hlex;
    int     Hnum;		 /* eventno when inserted into history list  */
    int     Href;
    time_t  Htime;
    Char   *histline;
    struct Hist *Hnext, *Hprev;         /* doubly linked list */
    unsigned Hhash;                     /* hash value of command line */
}       Histlist IZERO_STRUCT;

extern struct wordent paraml;	/* Current lexical word list */
EXTERN int     eventno;		/* Next events number */
EXTERN int     lastev;		/* Last event reference (default) */

EXTERN Char    HIST;		/* history invocation character */
EXTERN Char    HISTSUB;		/* auto-substitute character */
EXTERN Char    PRCH;		/* Prompt symbol for regular users */
EXTERN Char    PRCHROOT;	/* Prompt symbol for root */

/*
 * For operating systems with single case filenames (OS/2)
 */
#ifdef CASE_INSENSITIVE
# ifdef WIDE_STRINGS
#  define samecase(x) (towlower(x))
# else
#  define samecase(x) (isupper((unsigned char)(x)) ? \
		       tolower((unsigned char)(x)) : (x))
# endif
#else
# define samecase(x) (x)
#endif /* CASE_INSENSITIVE */

/*
 * strings.h:
 */
#ifndef SHORT_STRINGS
#define Strchr(a, b)  		strchr(a, b)
#define Strrchr(a, b)  		strrchr(a, b)
#define Strcat(a, b)  		strcat(a, b)
#define Strncat(a, b, c) 	strncat(a, b, c)
#define Strcpy(a, b)  		strcpy(a, b)
#define Strncpy(a, b, c) 	strncpy(a, b, c)
#define Strlen(a)		strlen(a)
#define Strcmp(a, b)		strcmp(a, b)
#define Strncmp(a, b, c)	strncmp(a, b, c)
#define Strcasecmp(a, b)	strcasecmp(a, b)

#define Strspl(a, b)		strspl(a, b)
#define Strnsave(a, b)		strnsave(a, b)
#define Strsave(a)		strsave(a)
#define Strend(a)		strend(a)
#define Strstr(a, b)		strstr(a, b)

#define str2short(a) 		(a)
#define blk2short(a) 		saveblk(a)
#define short2blk(a) 		saveblk(a)
#define short2str(a) 		caching_strip(a)
#else
#if defined(WIDE_STRINGS) && !defined(UTF16_STRINGS)
#define Strchr(a, b)		wcschr(a, b)
#define Strrchr(a, b)		wcsrchr(a, b)
#define Strcat(a, b)  		wcscat(a, b)
#define Strncat(a, b, c) 	wcsncat(a, b, c)
#define Strcpy(a, b)  		wcscpy(a, b)
#define Strncpy(a, b, c)	wcsncpy(a, b, c)
#define Strlen(a)		wcslen(a)
#define Strcmp(a, b)		wcscmp(a, b)
#define Strncmp(a, b, c)	wcsncmp(a, b, c)
#else
#define Strchr(a, b)		s_strchr(a, b)
#define Strrchr(a, b) 		s_strrchr(a, b)
#define Strcat(a, b)  		s_strcat(a, b)
#define Strncat(a, b, c) 	s_strncat(a, b, c)
#define Strcpy(a, b)  		s_strcpy(a, b)
#define Strncpy(a, b, c)	s_strncpy(a, b, c)
#define Strlen(a)		s_strlen(a)
#define Strcmp(a, b)		s_strcmp(a, b)
#define Strncmp(a, b, c)	s_strncmp(a, b, c)
#endif
#define Strcasecmp(a, b)	s_strcasecmp(a, b)

#define Strspl(a, b)		s_strspl(a, b)
#define Strnsave(a, b)		s_strnsave(a, b)
#define Strsave(a)		s_strsave(a)
#define Strend(a)		s_strend(a)
#define Strstr(a, b)		s_strstr(a, b)
#endif 

/*
 * setname is a macro to save space (see sh.err.c)
 */
EXTERN const char   *bname;

#define	setname(a)	(bname = (a))

#ifdef VFORK
EXTERN Char   *Vsav;
EXTERN Char   *Vdp;
EXTERN Char   *Vexpath;
EXTERN char  **Vt;
#endif /* VFORK */

EXTERN Char  **evalvec;
EXTERN Char   *evalp;

extern struct mesg {
    const char   *iname;	/* name from /usr/include */
    const char *pname;		/* print name */
} mesg[];

/* word_chars is set by default to WORD_CHARS (or WORD_CHARS_VI) but can
   be overridden by the wordchars variable--if unset, reverts to
   WORD_CHARS (or WORD_CHARS_VI) */

EXTERN Char   *word_chars;

#define WORD_CHARS "*?_-.[]~="	/* default chars besides alnums in words */
#define WORD_CHARS_VI "_"	/* default chars besides alnums in words */

EXTERN Char   *STR_SHELLPATH;

#ifdef _PATH_BSHELL
EXTERN Char   *STR_BSHELL;
#endif 
EXTERN Char   *STR_WORD_CHARS;
EXTERN Char   *STR_WORD_CHARS_VI;
EXTERN Char  **STR_environ IZERO;

extern int     dont_free;	/* Tell free that we are in danger if we free */

extern Char    *INVPTR;
extern Char    **INVPPTR;

extern char    *progname;
extern int	tcsh;
extern int	xlate_cr;
extern int	output_raw;
extern int	lbuffed;
extern time_t	Htime;
extern int	numeof;
extern int 	insource;
extern char	linbuf[];
extern char 	*linp;
extern int	nsig;
#ifdef VFORK
extern int	use_fork;
#endif
extern int	tellwhat;
extern int	NoNLSRebind;
#if !HAVE_DECL_ENVIRON
extern char   **environ;
#endif

#include "tc.h"

#ifndef WINNT_NATIVE
# ifdef NLS_CATALOGS
#  ifdef HAVE_FEATURES_H
#   include <features.h>
#  endif
#  ifdef HAVE_NL_LANGINFO
#   include <langinfo.h>
#  endif
#  ifdef __uxps__
#   define gettxt gettxt_ds
#  endif
#  include <nl_types.h>
#  ifdef __uxps__
#   undef gettxt
#  endif
EXTERN nl_catd catd;
#  if defined(HAVE_ICONV) && defined(HAVE_NL_LANGINFO)
#   define CGETS(b, c, d)	iconv_catgets(catd, b, c, d)
#  else
#   define CGETS(b, c, d)	xcatgets(catd, b, c, d)
#  endif
#  define CSAVS(b, c, d)	strsave(CGETS(b, c, d))
# else
#  define CGETS(b, c, d)	d
#  define CSAVS(b, c, d)	d
# endif 
#else /* WINNT_NATIVE */
# define CGETS(b, c, d)	nt_cgets( b, c, d)
# define CSAVS(b, c, d)	strsave(CGETS(b, c, d))
#endif /* WINNT_NATIVE */

#if defined(FILEC)
extern int    filec;
#endif /* FILEC */

#include "sh.decls.h"
/*
 * Since on some machines characters are unsigned, and the signed
 * keyword is not universally implemented, we treat all characters
 * as unsigned and sign extend them where we need.
 */
#define SIGN_EXTEND_CHAR(a)	(((a) & 0x80) ? ((a) | ~0x7f) : (a))

/*
 * explanation for use by the "--help" option
 */
#define HELP_STRING "\
-b file		batch mode, read and execute commands from `file' \n\
-c command	run `command' from next argument \n\
-d		load directory stack from `~/.cshdirs' \n\
-Dname[=value]	define environment variable `name' to `value' (DomainOS only) \n\
-e		exit on any error \n\
-f		start faster by ignoring the start-up file \n\
-F		use fork() instead of vfork() when spawning (ConvexOS only) \n\
-i		interactive, even when input is not from a terminal \n\
-l		act as a login shell, must be the only option specified \n\
-m		load the start-up file, whether or not owned by effective user \n\
-n file		no execute mode, just check syntax of the following `file' \n\
-q		accept SIGQUIT for running under a debugger \n\
-s		read commands from standard input \n\
-t		read one line from standard input \n\
-v		echo commands after history substitution \n\
-V		like -v but including commands read from the start-up file \n\
-x		echo commands immediately before execution \n\
-X		like -x but including commands read from the start-up file \n\
--help		print this message and exit \n\
--version	print the version shell variable and exit \n\
\nSee the tcsh(1) manual page for detailed information.\n"

#include "tc.nls.h"

#endif /* _h_sh */
