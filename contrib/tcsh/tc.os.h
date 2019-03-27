/* $Header: /p/tcsh/cvsroot/tcsh/tc.os.h,v 3.105 2011/02/04 18:00:26 christos Exp $ */
/*
 * tc.os.h: Shell os dependent defines
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
#ifndef _h_tc_os
#define _h_tc_os

#ifdef notdef 
/*
 * for SVR4 and linux we used to fork pipelines backwards. 
 * This should not be needed any more.
 * more info in sh.sem.c
 */
# define BACKPIPE
#endif /* notdef */

#ifdef __CYGWIN__
#  undef NOFILE
#  define NOFILE sysconf(_SC_OPEN_MAX)
#endif

#ifdef   _VMS_POSIX
# ifndef  NOFILE 
#  define  NOFILE 64
# endif /* NOFILE */
# define  nice(a)       setprio((getpid()),a)
# include <sys/time.h>    /* for time stuff in tc.prompt.c */
# include <limits.h>
#endif /* atp vmsposix */

#if defined(DECOSF1) || defined(HPUXVERSION)
# include <sys/signal.h>
#endif /* DECOSF1 || HPUXVERSION */

#ifdef DECOSF1
# include <sys/ioctl.h>
#endif /* DECOSF1 */

#if defined(OPEN_MAX) && !defined(NOFILE)
# define NOFILE OPEN_MAX
#endif /* OPEN_MAX && !NOFILE */

#if defined(USR_NFDS) && !defined(NOFILE)
# define NOFILE USR_NFDS
#endif /* USR_NFDS && !NOFILE */

#ifndef NOFILE
# define NOFILE 256
#endif /* NOFILE */

#ifdef OREO
# include <sys/time.h>
# ifdef notdef
  /* Don't include it, because it defines things we don't really have */
#  include <sys/resource.h>	
# endif /* notdef */
# ifdef POSIX
#  include <sys/tty.h>
#  include <termios.h>
# endif /* POSIX */
#endif /* OREO */

#ifdef __QNXNTO__
#include <sys/resource.h>
#include <fcntl.h>
#undef O_TEXT
#undef O_BINARY
#endif

#ifdef convex
# include <sys/dmon.h>
#endif /* convex */

#ifdef titan
extern int end;
#endif /* titan */

#ifdef hpux
# ifdef lint
/*
 * Hpux defines struct ucred, in <sys/user.h>, but if I include that
 * then I need to include the *world*
 * [all this to pass lint cleanly!!!]
 * so I define struct ucred here...
 */
struct ucred {
    int     foo;
};
# endif /* lint */

/*
 * hpux 7.0 does not define it
 */
# ifndef CSUSP
#  define CSUSP 032
# endif	/* CSUSP */

# include <signal.h>
# if !defined(hp9000s500) && !(defined(SIGRTMAX) || defined(SIGRTMIN))
/*
 * hpux < 7
 */
#  include <sys/bsdtty.h>
# endif /* !hp9000s500 && !(SIGRTMAX || SIGRTMIN) */

# ifndef TIOCSTI
#  include <sys/strtio.h>
# endif
#endif /* hpux */

/*
 * ISC does not define CSUSP
 */
#ifdef ISC
# ifndef CSUSP
#  define CSUSP 032
# endif	/* CSUSP */
# if defined(POSIX) && !defined(TIOCGWINSZ)
/*
 * ISC defines this only in termio.h. If we are using POSIX and include
 * termios.h, then we define it ourselves so that window resizing works.
 */
#  define TIOCGWINSZ      (('T'<<8)|104)
# endif /* POSIX && !TIOCGWINSZ */
#endif /* ISC */

#ifdef ISC202
# undef TIOCGWINSZ
#endif /* ISC202 */

/*
 * XXX: This will be changed soon to 
 * #if (SYSVREL > 0) && defined(TIOCGWINSZ)
 * If that breaks on your machine, let me know.
 *
 * It would break on glibc, where all this is
 * defined in <termios.h>. Wrapper added.
 */
#if !defined(__linux__) && !defined(__GNU__) && !defined(__GLIBC__) && !defined(_VMS_POSIX)
# if defined(INTEL) || defined(u3b2) || defined (u3b5) || defined(ub15) || defined(u3b20d) || defined(ISC) || defined(SCO) || defined(tower32)
#  ifdef TIOCGWINSZ
/*
 * for struct winsiz
 */
#   include <sys/stream.h>
#   include <sys/ptem.h>
#  endif /* TIOCGWINSZ */
# endif /* INTEL || u3b2 || u3b5 || ub15 || u3b20d || ISC || SCO || tower32 */
#endif /* !glibc && !_VMS_POSIX */

#ifdef IRIS4D
# include <sys/time.h>
# include <sys/resource.h>
#endif /* IRIS4D */

/*
 * For some versions of system V software, specially ones that use the 
 * Wollongong Software TCP/IP, the FIOCLEX, FIONCLEX, FIONBIO calls
 * might not work correctly for file descriptors [they work only for
 * sockets]. So we try to use first the fcntl() and we only use the
 * ioctl() form, only if we don't have the fcntl() one.
 *
 * From: scott@craycos.com (Scott Bolte)
 */
#ifndef WINNT_NATIVE
# ifdef F_SETFD
#  ifndef FD_CLOEXEC
#   define FD_CLOEXEC 1
#  endif
#  define close_on_exec(fd, v) fcntl((fd), F_SETFD, ((v) ? FD_CLOEXEC : 0))
# else /* !F_SETFD */
#  ifdef FIOCLEX
#   define close_on_exec(fd, v) ioctl((fd), ((v) ? FIOCLEX : FIONCLEX), NULL)
#  else /* !FIOCLEX */
#   define close_on_exec(fd, v)	/* Nothing */
#  endif /* FIOCLEX */
# endif /* F_SETFD */
#else /* WINNT_NATIVE */
# define close_on_exec(fd, v) nt_close_on_exec((fd),(v))
#endif /* !WINNT_NATIVE */

/*
 * Stat
 */
#ifdef ISC
/* these are not defined for _POSIX_SOURCE under ISC 2.2 */
# ifndef S_IFMT
#  define S_IFMT	0170000		/* type of file */
#  define S_IFDIR	0040000		/* directory */
#  define S_IFCHR	0020000		/* character special */
#  define S_IFBLK	0060000		/* block special */
#  define S_IFREG	0100000		/* regular */
#  define S_IFIFO	0010000		/* fifo */
#  define S_IFNAM	0050000		/* special named file */
#  ifndef ISC202
#   define S_IFLNK	0120000		/* symbolic link */
#  endif /* ISC202 */
# endif /* S_IFMT */
#endif /* ISC */

#ifdef STAT_MACROS_BROKEN
# undef S_ISDIR
# undef S_ISCHR
# undef S_ISBLK
# undef S_ISREG
# undef S_ISFIFO
# undef S_ISNAM
# undef S_ISLNK
# undef S_ISSOCK
#endif /* STAT_MACROS_BROKEN */

#ifdef S_IFMT
# if !defined(S_ISDIR) && defined(S_IFDIR)
#  define S_ISDIR(a)	(((a) & S_IFMT) == S_IFDIR)
# endif	/* ! S_ISDIR && S_IFDIR */
# if !defined(S_ISCHR) && defined(S_IFCHR)
#  define S_ISCHR(a)	(((a) & S_IFMT) == S_IFCHR)
# endif /* ! S_ISCHR && S_IFCHR */
# if !defined(S_ISBLK) && defined(S_IFBLK)
#  define S_ISBLK(a)	(((a) & S_IFMT) == S_IFBLK)
# endif	/* ! S_ISBLK && S_IFBLK */
# if !defined(S_ISREG) && defined(S_IFREG)
#  define S_ISREG(a)	(((a) & S_IFMT) == S_IFREG)
# endif	/* ! S_ISREG && S_IFREG */
# if !defined(S_ISFIFO) && defined(S_IFIFO)
#  define S_ISFIFO(a)	(((a) & S_IFMT) == S_IFIFO)
# endif	/* ! S_ISFIFO && S_IFIFO */
# if !defined(S_ISNAM) && defined(S_IFNAM)
#  define S_ISNAM(a)	(((a) & S_IFMT) == S_IFNAM)
# endif	/* ! S_ISNAM && S_IFNAM */
# if !defined(S_ISLNK) && defined(S_IFLNK)
#  define S_ISLNK(a)	(((a) & S_IFMT) == S_IFLNK)
# endif	/* ! S_ISLNK && S_IFLNK */
# if !defined(S_ISSOCK) && defined(S_IFSOCK)
#  define S_ISSOCK(a)	(((a) & S_IFMT) == S_IFSOCK)
# endif	/* ! S_ISSOCK && S_IFSOCK */
#endif /* S_IFMT */

#ifdef tower32
/* The header files lie; we really don't have symlinks */
# undef S_ISLNK
# undef S_IFLNK
#endif /* tower32 */

#ifndef S_IREAD
# define S_IREAD 0000400
#endif /* S_IREAD */
#ifndef S_IROTH
# define S_IROTH (S_IREAD >> 6)
#endif /* S_IROTH */
#ifndef S_IRGRP
# define S_IRGRP (S_IREAD >> 3)
#endif /* S_IRGRP */
#ifndef S_IRUSR
# define S_IRUSR S_IREAD
#endif /* S_IRUSR */

#ifndef S_IWRITE
# define S_IWRITE 0000200
#endif /* S_IWRITE */
#ifndef S_IWOTH
# define S_IWOTH (S_IWRITE >> 6)
#endif /* S_IWOTH */
#ifndef S_IWGRP
# define S_IWGRP (S_IWRITE >> 3)
#endif /* S_IWGRP */
#ifndef S_IWUSR
# define S_IWUSR S_IWRITE
#endif /* S_IWUSR */

#ifndef S_IEXEC
# define S_IEXEC 0000100
#endif /* S_IEXEC */
#ifndef S_IXOTH
# define S_IXOTH (S_IEXEC >> 6)
#endif /* S_IXOTH */
#ifndef S_IXGRP
# define S_IXGRP (S_IEXEC >> 3)
#endif /* S_IXGRP */
#ifndef S_IXUSR
# define S_IXUSR S_IEXEC
#endif /* S_IXUSR */

#ifndef S_ISUID
# define S_ISUID 0004000 	/* setuid */
#endif /* S_ISUID */
#ifndef S_ISGID	
# define S_ISGID 0002000	/* setgid */
#endif /* S_ISGID */
#ifndef S_ISVTX
# define S_ISVTX 0001000	/* sticky */
#endif /* S_ISVTX */
#ifndef S_ENFMT
# define S_ENFMT S_ISGID	/* record locking enforcement flag */
#endif /* S_ENFMT */

/* the following macros are for POSIX conformance */
#ifndef S_IRWXU
# define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)
#endif /* S_IRWXU */
#ifndef S_IRWXG
# define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)
#endif /* S_IRWXG */
#ifndef S_IRWXO
# define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)
#endif /* S_IRWXO */

/*
 * Access()
 */
#ifndef F_OK
# define F_OK 0
#endif /* F_OK */
#ifndef X_OK
# define X_OK 1
#endif /* X_OK */
#ifndef W_OK
# define W_OK 2
#endif /* W_OK */
#ifndef R_OK
# define R_OK 4
#endif /* R_OK */

/*
 * Open()
 */
#ifndef O_RDONLY
# define O_RDONLY	0
#endif /* O_RDONLY */
#ifndef O_WRONLY
# define O_WRONLY	1
#endif /* O_WRONLY */
#ifndef O_RDWR
# define O_RDWR		2
#endif /* O_RDWR */
#ifndef O_TEMPORARY
# define O_TEMPORARY	0
#endif /* O_TEMPORARY */
#ifndef O_EXCL
# define O_EXCL		0
#endif /* O_EXCL */
#ifndef O_LARGEFILE
# define O_LARGEFILE	0
#endif /* O_LARGEFILE */
#ifndef O_CREAT
# define O_CREAT	0
#endif /* O_CREAT */

/*
 * Lseek()
 */
#ifndef L_SET
# ifdef SEEK_SET
#  define L_SET		SEEK_SET
# else /* !SEEK_SET */
#  define L_SET		0
# endif	/* SEEK_SET */
#endif /* L_SET */
#ifndef L_INCR
# ifdef SEEK_CUR
#  define L_INCR	SEEK_CUR
# else /* !SEEK_CUR */
#  define L_INCR	1
# endif	/* SEEK_CUR */
#endif /* L_INCR */
#ifndef L_XTND
# ifdef SEEK_END
#  define L_XTND	SEEK_END
# else /* !SEEK_END */
#  define L_XTND	2
# endif /* SEEK_END */
#endif /* L_XTND */

#if !defined (HAVE_SETPGID) && !defined (SETPGRP_VOID)
# define setpgid(pid, pgrp)	setpgrp(pid, pgrp)
#endif

#if defined(BSDJOBS) && !(defined(POSIX) && defined(POSIXJOBS))
# define NEEDtcgetpgrp
#endif /* BSDJOBS && !(POSIX && POSIXJOBS) */

#ifdef RENO 
/*
 * RENO has this broken. It is fixed on 4.4BSD
 */
# define NEEDtcgetpgrp
#endif /* RENO */

#ifdef SXA
# ifndef _BSDX_
/*
 * Only needed in the system V environment.
 */
#  define setrlimit 	bsd_setrlimit
#  define getrlimit	bsd_getrlimit
# endif	/* _BSDX_ */
#endif /* SXA */

#if defined(_MINIX) || defined(__EMX__)
# define HAVENOLIMIT
/*
 * Minix does not have these, so...
 */
# define getpgrp		getpid
#endif /* _MINIX || __EMX__ */

#ifdef __EMX__
/* XXX: How can we get the tty name in emx? */
# define ttyname(fd) (isatty(fd) ? "/dev/tty" : NULL)
#endif /* __EMX__ */

#ifndef S_IFLNK
# define lstat stat
#endif /* S_IFLNK */


#if defined(BSDTIMES) && !defined(_SEQUENT_)
typedef struct timeval timeval_t;
#endif /* BSDTIMES && ! _SEQUENT_ */

#ifdef NeXT
/*
 * From Tony_Mason@transarc.com, override NeXT's malloc stuff.
 */
# define malloc tcsh_malloc
# define calloc tcsh_calloc
# define realloc tcsh_realloc
# define free tcsh_free
#endif /* NeXT */

#if defined(HAVE_GETHOSTNAME) && !HAVE_DECL_GETHOSTNAME
extern int gethostname (char *, int);
#endif

#ifndef GETPGRP_VOID
# define mygetpgrp()    getpgrp(0)
#else
# define mygetpgrp()    getpgrp()
#endif

#if !defined(POSIX) || defined(SUNOS4) || defined(UTekV) || defined(sysV88)
extern time_t time();
extern char *getenv();
extern int atoi();
# ifndef __EMX__
extern char *ttyname();
# endif /* __EMX__ */


# if defined(SUNOS4)
#  ifndef toupper
extern int toupper (int);
#  endif /* toupper */
#  ifndef tolower
extern int tolower (int);
#  endif /* tolower */
extern caddr_t sbrk (int);
# else /* !SUNOS4 */
#  ifndef WINNT_NATIVE
#   ifdef hpux
extern void abort();
extern void qsort();
#   endif /* hpux */
#  endif /* !WINNT_NATIVE */
# endif	/* SUNOS4 */
#ifndef _CX_UX
extern void perror();
#endif

# ifdef BSD
extern uid_t getuid(), geteuid();
extern gid_t getgid(), getegid();
# endif /* BSD */

# ifdef SYSMALLOC
extern memalign_t malloc();
extern memalign_t realloc();
extern memalign_t calloc();
extern void free();
# endif	/* SYSMALLOC */

# ifdef BSDJOBS
#  ifdef BSDTIMES
#   ifdef __MACHTEN__
extern pid_t wait3();
#   endif /* __MACHTEN__ */
#  endif /* BSDTIMES */
# endif	/* BSDJOBS */

# if (!defined(fps500) && !defined(apollo) && !defined(__lucid) && !defined(HPBSD) && !defined(DECOSF1))
extern void setpwent();
extern void endpwent();
# endif /* !fps500 && !apollo && !__lucid && !HPBSD && !DECOSF1 */

# ifndef __STDC__
extern struct passwd *getpwuid(), *getpwnam(), *getpwent();
#  ifdef HAVE_SHADOW_H
extern struct spwd *getspnam(), *getspent();
#  endif /* HAVE_SHADOW_H */
#  if defined(HAVE_AUTH_H) && defined(HAVE_GETAUTHUID)
extern struct authorization *getauthuid();
#  endif /* HAVE_AUTH_H && HAVE_GETAUTHUID */
# endif /* __STDC__ */

# ifndef getcwd
extern char *getcwd();
# endif	/* getcwd */

#else /* POSIX || !SUNOS4 || !UTekV || !sysV88 */

# if (defined(SUNOS4) && !defined(__GNUC__)) || defined(_IBMR2) || defined(_IBMESA)
extern char *getvwd();
# endif	/* (SUNOS4 && ! __GNUC__) || _IBMR2 || _IBMESA */

# ifdef SCO
extern char *ttyname();   
# endif /* SCO */

# ifdef __clipper__
extern char *ttyname();   
# endif /* __clipper__ */

#endif /* !POSIX || SUNOS4 || UTekV || sysV88 */

#if defined(SUNOS4) && __GNUC__ == 2
/*
 * Somehow these are missing
 */
extern int ioctl (int, int, ...);
extern int readlink (const char *, char *, size_t);
extern void setgrent (void);
extern void endgrent (void);
# ifdef REMOTEHOST
#  ifndef _SOCKLEN_T	/* Avoid Solaris 2.7 bogosity. */
struct sockaddr;
extern int getpeername (int, struct sockaddr *, int *);
#  endif /* _SOCKLEN_T */
# endif /* REMOTEHOST */
#endif /* SUNOS4 && __GNUC__ == 2 */

#if (defined(BSD) && !defined(BSD4_4)) || defined(SUNOS4) 
# if defined(__alpha) && defined(__osf__) && DECOSF1 < 200
extern void bcopy	(const void *, void *, size_t);
#  define memmove(a, b, c) (bcopy((char *) (b), (char *) (a), (int) (c)), a)
# endif /* __alpha && __osf__ && DECOSF1 < 200 */
#endif /* (BSD && !BSD4_4) || SUNOS4 */

#ifdef SUNOS4
# include <memory.h>	/* memset should be declared in <string.h> but isn't */
#endif /* SUNOS4 */

#if SYSVREL == 4
# ifdef REMOTEHOST
/* Irix6 defines getpeername(int, void *, int *) which conflicts with
   the definition below. */
#  if !defined(__sgi) && !defined(_OSD_POSIX) && !defined(__MVS__)
#   ifndef _SOCKLEN_T	/* Avoid Solaris 2.7 bogosity. */
struct sockaddr;
extern int getpeername (int, struct sockaddr *, int *);
#   endif /* _SOCKLEN_T */
#  endif /* !__sgi && !_OSD_POSIX && !__MVS__ */
# endif /* REMOTEHOST */
# ifndef BSDTIMES
extern int getrlimit (int, struct rlimit *);
extern int setrlimit (int, const struct rlimit *);
# endif /* !BSDTIMES */
# if defined(SOLARIS2)
extern char *strerror (int);
# endif /* SOLARIS2 */
#endif /* SYSVREL == 4 */

#if defined(__alpha) && defined(__osf__) && DECOSF1 < 200
/* These are ok for 1.3, but conflict with the header files for 2.0 */
extern char *sbrk (ssize_t);
extern int ioctl (int, unsigned long, char *);
extern pid_t vfork (void);
extern int killpg (pid_t, int);
#endif /* __osf__ && __alpha && DECOSF1 < 200 */

#ifndef va_copy
# ifdef __va_copy
#  define va_copy(DEST, SRC) __va_copy(DEST, SRC)
# else
#  define va_copy(DEST, SRC) memcpy(&(DEST), &(SRC), sizeof(va_list))
# endif
#endif

#if defined(__CYGWIN__) && !defined(NO_CRYPT)
extern char *cygwin_xcrypt(struct passwd *, const char *, const char *);
#endif /* __CYGWIN__ && !NO_CRYPT */

#endif /* _h_tc_os */
