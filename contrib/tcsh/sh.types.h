/* $Header: /p/tcsh/cvsroot/tcsh/sh.types.h,v 3.43 2006/01/12 19:55:38 christos Exp $ */
/* sh.types.h: Do the necessary typedefs for each system.
 *             Up till now I avoided making this into a separate file
 *	       But I just wanted to eliminate the whole mess from sh.h
 *	       In reality this should not be here! It is OS and MACHINE
 *	       dependent, even between different revisions of OS's...
 *	       Ideally there should be a way in c, to find out if something
 *	       was typedef'ed, but unfortunately we rely in cpp kludges.
 *	       Someday, this file will be removed... 
 *						
 *						christos
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
#ifndef _h_sh_types
#define _h_sh_types


/***
 *** LynxOS 2.1
 ***/
#ifdef Lynx
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
#endif

/***
 *** MachTen 
 ***/
#ifdef __MACHTEN__
# ifndef _PID_T
#  define _PID_T
# endif
#endif


/***
 *** Suns running sunos3.x - sunos4.1.x
 ***/
#if (defined(sun) || defined(__sun__)) && SYSVREL == 0
# ifndef _PTR_T
#  define _PTR_T 
#   ifdef __GNUC__
    typedef void * ptr_t;
#   else
    typedef char * ptr_t;
#   endif /* __GNUC__ */
# endif /* _PTR_T */
# ifndef __sys_stdtypes_h
#  define __sys_stdtypes_h
#   ifndef __lucid
     typedef int pid_t;
     typedef unsigned int speed_t;
#   endif
# endif /* __sys_stdtypes.h */
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
# ifndef _SPEED_T
#  define _SPEED_T
# endif /* _SPEED_T */
#endif /* (sun || __sun__) && SYSVREL == 0 */


/***
 *** Hp's running hpux 7.0 or 8.0
 ***/
#ifdef __hpux

# ifndef _PTR_T
#  define _PTR_T 
    typedef void * ptr_t;
# endif /* _PTR_T */

# ifndef _PID_T
#  define _PID_T
    typedef long pid_t;
# endif /* _PID_T */

# ifndef _SPEED_T
   /* I thought POSIX was supposed to protect all typedefs! */
#  define _SPEED_T
# endif /* _SPEED_T */

# if HPUXVERSION < 1100	/* XXX: Not true for 11.0 */
extern uid_t getuid(), geteuid();
extern gid_t getgid(), getegid();
extern pid_t getpid();
extern pid_t fork();
extern void perror();
extern void _exit();
extern void abort();
extern void qsort();
extern void free();
extern unsigned int alarm();
extern unsigned int sleep();
# endif /* HPUXVERSION < 1100 */
# if HPUXVERSION < 800	/* XXX: Not true for 8.0 */
extern char *sbrk();
# endif /* HPUXVERSION < 800 */
#endif /* __hpux */

#if (defined(_MINIX) && !defined(_MINIX_VMD)) || defined(__EMX__)
typedef char * caddr_t;
#endif /* (_MINIX && !_MINIX_VMD) || __EMX__ */

/***
 *** hp9000s500 running hpux-5.2
 ***/
#ifdef hp9000s500
# ifndef _PTR_T
#  define _PTR_T
    typedef char * ptr_t;
# endif /* _PTR_T */
#endif /* hp9000s500 */

/***
 *** Data General AViiON 88000 or Pentium, running dgux 5.4R3 or R4.11
 ***/
#ifdef DGUX
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
#endif /* DGUX */


/***
 *** BSD RENO advertises itself as POSIX, but
 *** it is missing speed_t 
 ***/
#ifdef RENO
# ifndef _SPEED_T
#  define _SPEED_T
   typedef unsigned int speed_t; 
# endif /* _SPEED_T */
#endif /* RENO */


/***
 *** NeXT OS 3.x
 ***/ 
#ifdef NeXT
# ifndef _SPEED_T
#  define _SPEED_T
   typedef unsigned int speed_t; 
# endif /* _SPEED_T */
#endif /* NeXT */

/***
 *** Utah's HPBSD
 *** some posix & 4.4 BSD changes (pid_t is a short)
 ***/
#ifdef HPBSD
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
#endif /* HPBSD */


/***
 *** Pyramid, BSD universe
 *** In addition to the size_t
 ***/
#ifdef pyr
# ifndef _PID_T
#  define _PID_T
   typedef short pid_t;
# endif /* _PID_T */
#endif /* pyr */


/***
 *** rs6000, ibm370, ps2, rt: running flavors of aix.
 ***/
#ifdef IBMAIX
# ifndef aiws
#  ifndef _PID_T
#   define _PID_T
#  endif /* _PID_T */
# endif /* !aiws */
# ifdef _IBMR2
#  ifndef _SPEED_T 
#   define _SPEED_T
#  endif /* _SPEED_T */
# endif /* _IBMR2 */
#endif /* IBMAIX */


/***
 *** Ultrix...
 ***/
#if defined(ultrix) || defined(__ultrix)
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
# ifndef _PTR_T
#  define _PTR_T
    typedef void * ptr_t;
# endif /* _PTR_T */
#endif /* ultrix || __ultrix */


/***
 *** Silicon graphics IRIS4D running IRIX3_3
 ***/
#if defined(IRIS4D) && defined(IRIX3_3)
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
#endif /* IRIS4D && IRIX3_3 */


/***
 *** Apple AUX.
 ***/
#ifdef OREO
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
#endif /* OREO */

/***
 *** Concurrent (Masscomp) running RTU 4.1A & RTU 5.0. 
 **** [RTU 6.0 from mike connor]
 *** Added, DAS DEC-90.
 ***/
#ifdef	masscomp
# ifdef RTU6
#  ifndef _PID_T
#   define _PID_T
#  endif /* _PID_T */
#  ifndef _SPEED_T
#   define _SPEED_T
#  endif /* _SPEED_T */
#endif /* RTU6 */
#endif	/* masscomp */

/* 
 * Motorola MPC running R32V2 (sysV88)
 */
#ifdef sysV88
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
#endif /* sysV88 */
 
/* 
 * Amdahl running UTS (Sys V3)
 */
#ifdef uts
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
#endif /* uts */

/* 
 * Tektronix XD88/10 running UTekV (Sys V3)
 */
#ifdef UTekV
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
#endif /* UTekV*/

/*
 * BBN Butterfly gp1000
 */
#ifdef butterfly
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
#endif /* butterfly */

/*
 * Alliant FX-2800/FX-80
 */
#ifdef alliant
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
# ifdef mc68000
   typedef int   pid_t; /* FX-80 */
# else
   typedef short pid_t;	/* FX-2800 */
# endif 
#endif /* alliant */

/*
 * DNIX
 */
#ifdef DNIX
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
#endif /* DNIX */

/*
 *  Apollo running Domain/OS SR10.3 or greater
 */
#ifdef apollo
# ifndef _PID_T
#  define _PID_T
   typedef int pid_t;	/* Older versions might not like that */
# endif /* _PID_T */
#endif /* apollo */

/***
 *** a pdp/11, running 2BSD
 ***/
#ifdef pdp11
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
#endif /* pdp11 */

/***
 *** a Harris, running CX/UX
 ***/
#ifdef _CX_UX
# ifndef _PID_T
#  define _PID_T
# endif /* _PID_T */
#endif /* _CX_UX */

/***
 *** Catch all for non POSIX and/or non ANSI systems.
 *** Systems up to spec *should* define these automatically
 *** I am open to suggestions on how to do this correctly!
 ***/

#ifndef POSIX

# ifndef _PID_T
#  define _PID_T
    typedef int pid_t;
# endif /* _PID_T */

# ifndef _SPEED_T
#  define _SPEED_T
    typedef unsigned int speed_t;
# endif /* _SPEED_T */

# ifndef _PTR_T
#  define _PTR_T 
    typedef char * ptr_t;
#endif /* _PTR_T */

# ifndef _IOCTL_T
#  define _IOCTL_T
    typedef char * ioctl_t;	/* Third arg of ioctl */
# endif /* _IOCTL_T */

#endif /* ! POSIX */



/***
 *** This is our own junk types.
 ***/
#ifndef _PTR_T
# define _PTR_T 
    typedef void * ptr_t;
#endif /* _PTR_T */

#ifndef _IOCTL_T
# define _IOCTL_T
    typedef void * ioctl_t;	/* Third arg of ioctl */
#endif /* _IOCTL_T */

#endif /* _h_sh_types */
