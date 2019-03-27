/* $Header: /p/tcsh/cvsroot/tcsh/sh.proc.h,v 3.16 2016/05/24 17:41:12 christos Exp $ */
/*
 * sh.proc.h: Process data structures and variables
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
#ifndef _h_sh_proc
#define _h_sh_proc
/*
 * C shell - process structure declarations
 */

/*
 * Structure for each process the shell knows about:
 *	allocated and filled by pcreate.
 *	flushed by pflush; freeing always happens at top level
 *	    so the interrupt level has less to worry about.
 *	processes are related to "friends" when in a pipeline;
 *	    p_friends links makes a circular list of such jobs
 */
struct process {
    struct process *p_next;	/* next in global "proclist" */
    struct process *p_friends;	/* next in job list (or self) */
    struct directory *p_cwd;	/* cwd of the job (only in head) */
    unsigned long p_flags;	/* various job status flags */
    unsigned char p_reason;	/* reason for entering this state */
    int     p_index;		/* shorthand job index */
    pid_t   p_parentid;		/* parent pid */
    pid_t   p_procid;
    pid_t   p_jobid;		/* pid of job leader */
    /* if a job is stopped/background p_jobid gives its pgrp */
#ifdef BSDTIMES
    struct timeval p_btime;	/* begin time */
    struct timeval p_etime;	/* end time */
    struct sysrusage p_rusage;
#else				/* BSDTIMES */
# ifdef _SEQUENT_
    timeval_t p_btime;		/* begin time */
    timeval_t p_etime;		/* end time */
    struct process_stats p_rusage;
# else				/* _SEQUENT_ */
#  ifndef POSIX
    time_t  p_btime;		/* begin time */
    time_t  p_etime;		/* end time */
    time_t  p_utime;		/* user time */
    time_t  p_stime;		/* system time */
#  else	/* POSIX */
    clock_t p_btime;		/* begin time */
    clock_t p_etime;		/* end time */
    clock_t p_utime;		/* user time */
    clock_t p_stime;		/* system time */
#  endif /* POSIX */
# endif /* _SEQUENT_ */
#endif /* BSDTIMES */
    Char   *p_command;		/* command */
};

/* flag values for p_flags */
#define	PRUNNING	(1<<0)	/* running */
#define	PSTOPPED	(1<<1)	/* stopped */
#define	PNEXITED	(1<<2)	/* normally exited */
#define	PAEXITED	(1<<3)	/* abnormally exited */
#define	PSIGNALED	(1<<4)	/* terminated by a signal != SIGINT */

#define	PALLSTATES	(PRUNNING|PSTOPPED|PNEXITED|PAEXITED| \
			 PSIGNALED|PINTERRUPTED)
#define	PNOTIFY		(1<<5)	/* notify async when done */
#define	PTIME		(1<<6)	/* job times should be printed */
#define	PAWAITED	(1<<7)	/* top level is waiting for it */
#define	PFOREGND	(1<<8)	/* started in shells pgrp */
#define	PDUMPED		(1<<9)	/* process dumped core */
#define	PDIAG		(1<<10)	/* diagnostic output also piped out */
#define	PPOU		(1<<11)	/* piped output */
#define	PREPORTED	(1<<12)	/* status has been reported */
#define	PINTERRUPTED	(1<<13)	/* job stopped via interrupt signal */
#define	PPTIME		(1<<14)	/* time individual process */
#define	PNEEDNOTE	(1<<15)	/* notify as soon as practical */
#define PBACKQ		(1<<16)	/* Process is `` evaluation */
#define PHUP		(1<<17)	/* Process is marked for SIGHUP on exit */
#define PBRACE		(1<<18)	/* Process is {} evaluation */

/* defines for arguments to pprint */
#define	NUMBER		0x001
#define	NAME		0x002
#define	REASON		0x004
#define	AMPERSAND	0x008
#define	FANCY		0x010
#define	SHELLDIR	0x020	/* print shell's dir if not the same */
#define	JOBDIR		0x040	/* print job's dir if not the same */
#define	AREASON		0x080
#define	JOBLIST		0x100

EXTERN struct process proclist IZERO_STRUCT;/* list head of all processes */

EXTERN struct process *pholdjob IZERO;	/* one level stack of current jobs */

EXTERN struct process *pcurrjob IZERO;	/* current job */
EXTERN struct process *pcurrent IZERO;	/* current job in table */
EXTERN struct process *pprevious IZERO;	/* previous job in table */

EXTERN int   pmaxindex IZERO;		/* current maximum job index */

#endif /* _h_sh_proc */
