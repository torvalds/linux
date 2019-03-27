/* $Header: /p/tcsh/cvsroot/tcsh/tc.wait.h,v 3.15 2011/02/04 18:00:26 christos Exp $ */
/*
 * tc.wait.h: <sys/wait.h> for machines that don't have it or have it and
 *	      is incorrect.
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
#ifndef _h_tc_wait
#define _h_tc_wait

/*
 * a little complicated #include <sys/wait.h>! :-(
 * We try to use the system's wait.h when we can...
 */

#if SYSVREL > 0 && !defined(__linux__) && !defined(__GNU__) && !defined(__GLIBC__)
# ifdef hpux
#  ifndef __hpux
#   define NEEDwait
#  else
#   ifndef POSIX
#    define _BSD
#   endif
#   ifndef _CLASSIC_POSIX_TYPES
#    define _CLASSIC_POSIX_TYPES
#   endif
#   include <sys/wait.h> /* 7.0 fixed it again */
#  endif /* __hpux */
# else /* hpux */
#  if (defined(OREO) || defined(IRIS4D) || defined(POSIX)) && !defined(_VMS_POSIX)
#   include <sys/wait.h>
#  else	/* OREO || IRIS4D || POSIX */
#   define NEEDwait
#  endif /* OREO || IRIS4D || POSIX */
# endif	/* hpux */
#else /* SYSVREL == 0 || glibc */
# ifdef _MINIX
#  undef NEEDwait
#  include "mi.wait.h"
# else
#  ifndef WINNT_NATIVE
#   include <sys/wait.h>
#  endif /* WINNT_NATIVE */
# endif /* _MINIX */
#endif /* SYSVREL == 0 || glibc */

#ifdef NEEDwait
/*
 *	This wait is for big-endians and little endians
 */
union wait {
    int     w_status;
# ifdef _SEQUENT_
    struct {
	unsigned short w_Termsig:7;
	unsigned short w_Coredump:1;
	unsigned short w_Retcode:8;
    }       w_T;
    struct {
	unsigned short w_Stopval:8;
	unsigned short w_Stopsig:8;
    }       w_S;
};

#  define w_termsig     w_T.w_Termsig
#  define w_coredump    w_T.w_Coredump
#  define w_retcode     w_T.w_Retcode
#  define w_stopval     w_S.w_Stopval
#  define w_stopsig     w_S.w_Stopsig
# else /* _SEQUENT_ */
#  if defined(vax) || defined(__vax__) || defined(i386) || defined(_I386) || defined(__i386__)
    union {
	struct {
	    unsigned int w_Termsig:7;
	    unsigned int w_Coredump:1;
	    unsigned int w_Retcode:8;
	    unsigned int w_Dummy:16;
	}       w_T;
	struct {
	    unsigned int w_Stopval:8;
	    unsigned int w_Stopsig:8;
	    unsigned int w_Dummy:16;
	}       w_S;
    }       w_P;
#  else /* mc68000 || sparc || ??? */
#    if defined(_CRAY) || defined(ANY_OTHER_64BIT_MACHINE)
#      define DUMMY_BITS	48
#    else /* _CRAY */
#      define DUMMY_BITS	16
#    endif /* _CRAY */
    union {
	struct {
	    unsigned int w_Dummy:DUMMY_BITS;
	    unsigned int w_Retcode:8;
	    unsigned int w_Coredump:1;
	    unsigned int w_Termsig:7;
	}       w_T;
	struct {
	    unsigned int w_Dummy:DUMMY_BITS;
	    unsigned int w_Stopsig:8;
	    unsigned int w_Stopval:8;
	}       w_S;
    }       w_P;
#  endif /* vax || __vax__ || i386 || _I386 || __i386__ */
};

#  define w_termsig	w_P.w_T.w_Termsig
#  define w_coredump	w_P.w_T.w_Coredump
#  define w_retcode	w_P.w_T.w_Retcode
#  define w_stopval	w_P.w_S.w_Stopval
#  define w_stopsig	w_P.w_S.w_Stopsig
# endif /* _SEQUENT_ */


# ifndef WNOHANG
#  define WNOHANG	1	/* dont hang in wait */
# endif

# ifndef WUNTRACED
#  define WUNTRACED	2	/* tell about stopped, untraced children */
# endif

# define WSTOPPED 0177
# define WIFSTOPPED(x)	((x).w_stopval == WSTOPPED)
# define WIFSIGNALED(x)	(((x).w_stopval != WSTOPPED) && ((x).w_termsig != 0))

#endif /* NEEDwait */

#endif /* _h_tc_wait */
