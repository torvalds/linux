/*	$OpenBSD: proc.h,v 1.5 2020/08/30 22:23:47 mortimer Exp $	*/
/*	$NetBSD: proc.h,v 1.7 1995/04/29 23:21:35 mycroft Exp $	*/

/*-
 * Copyright (c) 1980, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *
 *	@(#)proc.h	8.1 (Berkeley) 5/31/93
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
    short unsigned p_flags;	/* various job status flags */
    char    p_reason;		/* reason for entering this state */
    int     p_index;		/* shorthand job index */
    pid_t   p_pid;
    pid_t   p_jobid;		/* pid of job leader */
    /* if a job is stopped/background p_jobid gives its pgrp */
    struct timespec p_btime;	/* begin time */
    struct timespec p_etime;	/* end time */
    struct rusage p_rusage;
    Char   *p_command;		/* first PMAXLEN chars of command */
};

/* flag values for p_flags */
#define	PRUNNING	(1<<0)	/* running */
#define	PSTOPPED	(1<<1)	/* stopped */
#define	PNEXITED	(1<<2)	/* normally exited */
#define	PAEXITED	(1<<3)	/* abnormally exited */
#define	PSIGNALED	(1<<4)	/* terminated by a signal != SIGINT */

#define	PALLSTATES	(PRUNNING|PSTOPPED|PNEXITED|PAEXITED|PSIGNALED|PINTERRUPTED)
#define	PNOTIFY		(1<<5)	/* notify async when done */
#define	PTIME		(1<<6)	/* job times should be printed */
#define	PAWAITED	(1<<7)	/* top level is waiting for it */
#define	PFOREGND	(1<<8)	/* started in shells pgrp */
#define	PDUMPED		(1<<9)	/* process dumped core */
#define	PERR		(1<<10)	/* diagnostic output also piped out */
#define	PPOU		(1<<11)	/* piped output */
#define	PREPORTED	(1<<12)	/* status has been reported */
#define	PINTERRUPTED	(1<<13)	/* job stopped via interrupt signal */
#define	PPTIME		(1<<14)	/* time individual process */
#define	PNEEDNOTE	(1<<15)	/* notify as soon as practical */

#define	PMAXLEN		80

/* defines for arguments to pprint */
#define	NUMBER		01
#define	NAME		02
#define	REASON		04
#define	AMPERSAND	010
#define	FANCY		020
#define	SHELLDIR	040	/* print shell's dir if not the same */
#define	JOBDIR		0100	/* print job's dir if not the same */
#define	AREASON		0200

extern struct process proclist;	  /* list head of all processes */
extern bool    pnoprocesses;	  /* pchild found nothing to wait for */

extern struct process *pholdjob;  /* one level stack of current jobs */

extern struct process *pcurrjob;  /* current job */
extern struct process *pcurrent;  /* current job in table */
extern struct process *pprevious; /* previous job in table */

extern int    pmaxindex;	  /* current maximum job index */
