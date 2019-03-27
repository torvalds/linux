/* $Header: /p/tcsh/cvsroot/tcsh/sh.proc.c,v 3.134 2016/09/23 19:17:28 christos Exp $ */
/*
 * sh.proc.c: Job manipulations
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

RCSID("$tcsh: sh.proc.c,v 3.134 2016/09/23 19:17:28 christos Exp $")

#include "ed.h"
#include "tc.h"
#include "tc.wait.h"

#ifdef WINNT_NATIVE
#undef POSIX
#define POSIX
#endif /* WINNT_NATIVE */
#ifdef aiws
# undef HZ
# define HZ 16
#endif /* aiws */

#if defined(_BSD) || (defined(IRIS4D) && __STDC__) || defined(__lucid)
# define BSDWAIT
#endif /* _BSD || (IRIS4D && __STDC__) || __lucid */
#ifndef WTERMSIG
# define WTERMSIG(w)	(((union wait *) &(w))->w_termsig)
# ifndef BSDWAIT
#  define BSDWAIT
# endif /* !BSDWAIT */
#endif /* !WTERMSIG */
#ifndef WEXITSTATUS
# define WEXITSTATUS(w)	(((union wait *) &(w))->w_retcode)
#endif /* !WEXITSTATUS */
#ifndef WSTOPSIG
# define WSTOPSIG(w)	(((union wait *) &(w))->w_stopsig)
#endif /* !WSTOPSIG */

#ifdef __osf__
# ifndef WCOREDUMP
#  define WCOREDUMP(x) (_W_INT(x) & WCOREFLAG)
# endif
#endif

#ifndef WCOREDUMP
# ifdef BSDWAIT
#  define WCOREDUMP(w)	(((union wait *) &(w))->w_coredump)
# else /* !BSDWAIT */
#  define WCOREDUMP(w)	((w) & 0200)
# endif /* !BSDWAIT */
#endif /* !WCOREDUMP */

#ifndef JOBDEBUG
# define jobdebug_xprintf(x)	(void)0
# define jobdebug_flush()	(void)0
#else
# define jobdebug_xprintf(s)	xprintf s
# define jobdebug_flush()	flush()
#endif

/*
 * C Shell - functions that manage processes, handling hanging, termination
 */

#define BIGINDEX	9	/* largest desirable job index */

#ifdef BSDTIMES
# ifdef convex
/* use 'cvxrusage' to get parallel statistics */
static struct cvxrusage zru = {{0L, 0L}, {0L, 0L}, 0L, 0L, 0L, 0L,
				0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L, 0L,
				{0L, 0L}, 0LL, 0LL, 0LL, 0LL, 0L, 0L, 0L,
				0LL, 0LL, {0L, 0L, 0L, 0L, 0L}};
# else
static struct rusage zru;
# endif /* convex */
#else /* !BSDTIMES */
# ifdef _SEQUENT_
static struct process_stats zru = {{0L, 0L}, {0L, 0L}, 0, 0, 0, 0, 0, 0, 0,
				   0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
# else /* !_SEQUENT_ */
#  ifdef _SX
static struct tms zru = {0, 0, 0, 0}, lru = {0, 0, 0, 0};
#  else	/* !_SX */
static struct tms zru = {0L, 0L, 0L, 0L}, lru = {0L, 0L, 0L, 0L};
#  endif	/* !_SX */
# endif	/* !_SEQUENT_ */
#endif /* !BSDTIMES */

#ifndef BSDTIMES
static int timesdone;	/* shtimes buffer full ? */
#endif /* BSDTIMES */

#ifndef RUSAGE_CHILDREN
# define	RUSAGE_CHILDREN	-1
#endif /* RUSAGE_CHILDREN */

static	void		 pflushall	(void);
static	void		 pflush		(struct process *);
static	void		 pfree		(struct process *);
static	void		 pclrcurr	(struct process *);
static	void		 morecommand	(size_t);
static	void		 padd		(struct command *);
static	int		 pprint		(struct process *, int);
static	void		 ptprint	(struct process *);
static	void		 pads		(Char *);
static	void		 pkill		(Char **, int);
static	struct process	*pgetcurr	(struct process *);
static	void		 okpcntl	(void);
static	void		 setttypgrp	(int);

/*
 * pchild - call queued by the SIGCHLD signal
 *	indicating that at least one child has terminated or stopped
 *	thus at least one wait system call will definitely return a
 *	childs status.  Top level routines (like pwait) must be sure
 *	to mask interrupts when playing with the proclist data structures!
 */
void
pchild(void)
{
    struct process *pp;
    struct process *fp;
    pid_t pid;
#ifdef BSDWAIT
    union wait w;
#else /* !BSDWAIT */
    int     w;
#endif /* !BSDWAIT */
    int     jobflags;
#ifdef BSDTIMES
    struct sysrusage ru;
#else /* !BSDTIMES */
# ifdef _SEQUENT_
    struct process_stats ru;
    struct process_stats cpst1, cpst2;
    timeval_t tv;
# else /* !_SEQUENT_ */
    struct tms proctimes;

    if (!timesdone) {
	timesdone++;
	(void) times(&shtimes);
    }
# endif	/* !_SEQUENT_ */
#endif /* !BSDTIMES */

    jobdebug_xprintf(("pchild()\n"));

loop:
    jobdebug_xprintf(("Waiting...\n"));
    jobdebug_flush();
    errno = 0;			/* reset, just in case */

#ifndef WINNT_NATIVE
# ifdef BSDJOBS
#  ifdef BSDTIMES
#   ifdef convex
    /* use 'cvxwait' to get parallel statistics */
    pid = cvxwait(&w,
        (setintr && (intty || insource) ? WNOHANG | WUNTRACED : WNOHANG), &ru);
#   else
    /* both a wait3 and rusage */
#    if !defined(BSDWAIT) || defined(NeXT) || defined(MACH) || defined(__linux__) || defined(__GNU__) || defined(__GLIBC__) || (defined(IRIS4D) && SYSVREL <= 3) || defined(__lucid) || defined(__osf__)
#ifdef __ANDROID__ /* no wait3, only wait4 */
    pid = wait4(-1, &w,
       (setintr && (intty || insource) ? WNOHANG | WUNTRACED : WNOHANG), &ru);
#else
    pid = wait3(&w,
       (setintr && (intty || insource) ? WNOHANG | WUNTRACED : WNOHANG), &ru);
#endif /* __ANDROID__ */
#    else /* BSDWAIT */
    pid = wait3(&w.w_status,
       (setintr && (intty || insource) ? WNOHANG | WUNTRACED : WNOHANG), &ru);
#    endif /* BSDWAIT */
#   endif /* convex */
#  else /* !BSDTIMES */
#   ifdef _SEQUENT_
    (void) get_process_stats(&tv, PS_SELF, 0, &cpst1);
    pid = waitpid(-1, &w,
	    (setintr && (intty || insource) ? WNOHANG | WUNTRACED : WNOHANG));
    (void) get_process_stats(&tv, PS_SELF, 0, &cpst2);
    pr_stat_sub(&cpst2, &cpst1, &ru);
#   else	/* !_SEQUENT_ */
#    ifndef POSIX
    /* we have a wait3, but no rusage stuff */
    pid = wait3(&w.w_status,
	 (setintr && (intty || insource) ? WNOHANG | WUNTRACED : WNOHANG), 0);
#    else /* POSIX */
    pid = waitpid(-1, &w,
	    (setintr && (intty || insource) ? WNOHANG | WUNTRACED : WNOHANG));
#    endif /* POSIX */
#   endif /* !_SEQUENT_ */
#  endif	/* !BSDTIMES */
# else /* !BSDJOBS */
#  ifdef BSDTIMES
#   define HAVEwait3
    /* both a wait3 and rusage */
#   ifdef hpux
    pid = wait3(&w.w_status, WNOHANG, 0);
#   else	/* !hpux */
#     ifndef BSDWAIT
    pid = wait3(&w, WNOHANG, &ru);
#     else
    pid = wait3(&w.w_status, WNOHANG, &ru);
#     endif /* BSDWAIT */
#   endif /* !hpux */
#  else /* !BSDTIMES */
#   ifdef ODT  /* For Sco Unix 3.2.0 or ODT 1.0 */
#    define HAVEwait3
    pid = waitpid(-1, &w,
 	    (setintr && (intty || insource) ? WNOHANG | WUNTRACED : WNOHANG));
#   endif /* ODT */	    
#   if defined(aiws) || defined(uts)
#    define HAVEwait3
    pid = wait3(&w.w_status, 
	(setintr && (intty || insource) ? WNOHANG | WUNTRACED : WNOHANG), 0);
#   endif /* aiws || uts */
#   ifndef HAVEwait3
#    ifndef BSDWAIT
     /* no wait3, therefore no rusage */
     /* on Sys V, this may hang.  I hope it's not going to be a problem */
    pid = wait(&w);
#    else /* BSDWAIT */
     /* 
      * XXX: for greater than 3 we should use waitpid(). 
      * but then again, SVR4 falls into the POSIX/BSDJOBS category.
      */
    pid = wait(&w.w_status);
#    endif /* BSDWAIT */
#   endif /* !HAVEwait3 */
#  endif	/* !BSDTIMES */
# endif /* !BSDJOBS */
#else /* WINNT_NATIVE */
    pid = waitpid(-1, &w,
	    (setintr && (intty || insource) ? WNOHANG | WUNTRACED : WNOHANG));
#endif /* WINNT_NATIVE */

    jobdebug_xprintf(("parent %d pid %d, retval %x termsig %x retcode %x\n",
		      (int)getpid(), (int)pid, w, WTERMSIG(w),
		      WEXITSTATUS(w)));
    jobdebug_flush();

    if ((pid == 0) || (pid == -1)) {
	(void)handle_pending_signals();
	jobdebug_xprintf(("errno == %d\n", errno));
	if (errno == EINTR)
	    goto loop;
	goto end;
    }
    for (pp = proclist.p_next; pp != NULL; pp = pp->p_next)
	if (pid == pp->p_procid)
	    goto found;
#if !defined(BSDJOBS) && !defined(WINNT_NATIVE)
    /* this should never have happened */
    stderror(ERR_SYNC, pid);
    xexit(0);
#else /* BSDJOBS || WINNT_NATIVE */
    goto loop;
#endif /* !BSDJOBS && !WINNT_NATIVE */
found:
    pp->p_flags &= ~(PRUNNING | PSTOPPED | PREPORTED);
    if (WIFSTOPPED(w)) {
	pp->p_flags |= PSTOPPED;
	pp->p_reason = WSTOPSIG(w);
    }
    else {
	if (pp->p_flags & (PTIME | PPTIME) || adrof(STRtime))
#ifndef BSDTIMES
# ifdef _SEQUENT_
	    (void) get_process_stats(&pp->p_etime, PS_SELF, NULL, NULL);
# else	/* !_SEQUENT_ */
	    pp->p_etime = times(&proctimes);
# endif	/* !_SEQUENT_ */
#else /* BSDTIMES */
	    (void) gettimeofday(&pp->p_etime, NULL);
#endif /* BSDTIMES */


#if defined(BSDTIMES) || defined(_SEQUENT_)
	pp->p_rusage = ru;
#else /* !BSDTIMES && !_SEQUENT_ */
	(void) times(&proctimes);
	pp->p_utime = proctimes.tms_cutime - shtimes.tms_cutime;
	pp->p_stime = proctimes.tms_cstime - shtimes.tms_cstime;
	shtimes = proctimes;
#endif /* !BSDTIMES && !_SEQUENT_ */
	if (WIFSIGNALED(w)) {
	    if (WTERMSIG(w) == SIGINT)
		pp->p_flags |= PINTERRUPTED;
	    else
		pp->p_flags |= PSIGNALED;
	    if (WCOREDUMP(w))
		pp->p_flags |= PDUMPED;
	    pp->p_reason = WTERMSIG(w);
	}
	else {
	    pp->p_reason = WEXITSTATUS(w);
	    if (pp->p_reason != 0)
		pp->p_flags |= PAEXITED;
	    else
		pp->p_flags |= PNEXITED;
	}
    }
    jobflags = 0;
    fp = pp;
    do {
	if ((fp->p_flags & (PPTIME | PRUNNING | PSTOPPED)) == 0 &&
	    !child && adrof(STRtime) &&
#ifdef BSDTIMES
	    fp->p_rusage.ru_utime.tv_sec + fp->p_rusage.ru_stime.tv_sec
#else /* !BSDTIMES */
# ifdef _SEQUENT_
	    fp->p_rusage.ps_utime.tv_sec + fp->p_rusage.ps_stime.tv_sec
# else /* !_SEQUENT_ */
#  ifndef POSIX
	    (fp->p_utime + fp->p_stime) / HZ
#  else /* POSIX */
	    (fp->p_utime + fp->p_stime) / clk_tck
#  endif /* POSIX */
# endif /* !_SEQUENT_ */
#endif /* !BSDTIMES */
	    >= atoi(short2str(varval(STRtime))))
	    fp->p_flags |= PTIME;
	jobflags |= fp->p_flags;
    } while ((fp = fp->p_friends) != pp);
    pp->p_flags &= ~PFOREGND;
    if (pp == pp->p_friends && (pp->p_flags & PPTIME)) {
	pp->p_flags &= ~PPTIME;
	pp->p_flags |= PTIME;
    }
    if ((jobflags & (PRUNNING | PREPORTED)) == 0) {
	fp = pp;
	do {
	    if (fp->p_flags & PSTOPPED)
		fp->p_flags |= PREPORTED;
	} while ((fp = fp->p_friends) != pp);
	while (fp->p_procid != fp->p_jobid)
	    fp = fp->p_friends;
	if (jobflags & PSTOPPED) {
	    if (pcurrent && pcurrent != fp)
		pprevious = pcurrent;
	    pcurrent = fp;
	}
	else
	    pclrcurr(fp);
	if (jobflags & PFOREGND) {
	    if (!(jobflags & (PSIGNALED | PSTOPPED | PPTIME) ||
#ifdef notdef
		jobflags & PAEXITED ||
#endif /* notdef */
		fp->p_cwd == NULL ||
		!eq(dcwd->di_name, fp->p_cwd->di_name))) {
	    /* PWP: print a newline after ^C */
		if (jobflags & PINTERRUPTED) {
		    xputchar('\r' | QUOTE);
		    xputchar('\n');
		}
#ifdef notdef
		else if ((jobflags & (PTIME|PSTOPPED)) == PTIME)
		    ptprint(fp);
#endif /* notdef */
	    }
	}
	else {
	    if (jobflags & PNOTIFY || adrof(STRnotify)) {
	        xputchar('\r' | QUOTE);
		xputchar('\n');
		(void) pprint(pp, NUMBER | NAME | REASON);
		if ((jobflags & PSTOPPED) == 0)
		    pflush(pp);
		if (GettingInput) {
		    errno = 0;
		    (void) Rawmode();
#ifdef notdef
		    /*
		     * don't really want to do that, because it
		     * will erase our message in case of multi-line
		     * input
		     */
		    ClearLines();
#endif /* notdef */
		    ClearDisp();
		    Refresh();
		}
	    }
	    else {
		fp->p_flags |= PNEEDNOTE;
		neednote = 1;
	    }
	}
    }
#if defined(BSDJOBS) || defined(HAVEwait3) ||defined(WINNT_NATIVE)
    goto loop;
#endif /* BSDJOBS || HAVEwait3 */
 end:
    ;
}

void
pnote(void)
{
    struct process *pp;
    int     flags;

    neednote = 0;
    for (pp = proclist.p_next; pp != NULL; pp = pp->p_next) {
	if (pp->p_flags & PNEEDNOTE) {
	    pchild_disabled++;
	    cleanup_push(&pchild_disabled, disabled_cleanup);
	    pp->p_flags &= ~PNEEDNOTE;
	    flags = pprint(pp, NUMBER | NAME | REASON);
	    if ((flags & (PRUNNING | PSTOPPED)) == 0)
		pflush(pp);
	    cleanup_until(&pchild_disabled);
	}
    }
}


static void
pfree(struct process *pp)
{	
    xfree(pp->p_command);
    if (pp->p_cwd && --pp->p_cwd->di_count == 0)
	if (pp->p_cwd->di_next == 0)
	    dfree(pp->p_cwd);
    xfree(pp);
}


/*
 * pwait - wait for current job to terminate, maintaining integrity
 *	of current and previous job indicators.
 */
void
pwait(void)
{
    struct process *fp, *pp;

    /*
     * Here's where dead procs get flushed.
     */
    for (pp = (fp = &proclist)->p_next; pp != NULL; pp = (fp = pp)->p_next)
	if (pp->p_procid == 0) {
	    fp->p_next = pp->p_next;
	    pfree(pp);
	    pp = fp;
	}
    pjwait(pcurrjob);
}


/*
 * pjwait - wait for a job to finish or become stopped
 *	It is assumed to be in the foreground state (PFOREGND)
 */
void
pjwait(struct process *pp)
{
    struct process *fp;
    int     jobflags, reason;
    sigset_t oset, set, pause_mask;
    Char *reason_str;

    while (pp->p_procid != pp->p_jobid)
	pp = pp->p_friends;
    fp = pp;

    do {
	if ((fp->p_flags & (PFOREGND | PRUNNING)) == PRUNNING)
	  xprintf("%s", CGETS(17, 1, "BUG: waiting for background job!\n"));
    } while ((fp = fp->p_friends) != pp);
    /*
     * Now keep pausing as long as we are not interrupted (SIGINT), and the
     * target process, or any of its friends, are running
     */
    fp = pp;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGCHLD);
    (void)sigprocmask(SIG_BLOCK, &set, &oset);
    cleanup_push(&oset, sigprocmask_cleanup);
    pause_mask = oset;
    sigdelset(&pause_mask, SIGCHLD);
    sigaddset(&pause_mask, SIGINT);
    for (;;) {
	(void)handle_pending_signals();
	jobflags = 0;
	do
	    jobflags |= fp->p_flags;
	while ((fp = (fp->p_friends)) != pp);
	if ((jobflags & PRUNNING) == 0)
	    break;
	jobdebug_xprintf(("%d starting to sigsuspend for SIGCHLD on %d\n",
			  getpid(), fp->p_procid));
	sigsuspend(&pause_mask);
    }
    cleanup_until(&oset);
    jobdebug_xprintf(("%d returned from sigsuspend loop\n", getpid()));
#ifdef BSDJOBS
    if (tpgrp > 0)		/* get tty back */
	(void) tcsetpgrp(FSHTTY, tpgrp);
#endif /* BSDJOBS */
    if ((jobflags & (PSIGNALED | PSTOPPED | PTIME)) ||
	fp->p_cwd == NULL || !eq(dcwd->di_name, fp->p_cwd->di_name)) {
	if (jobflags & PSTOPPED) {
	    xputchar('\n');
	    if (adrof(STRlistjobs)) {
		Char   *jobcommand[3];

		jobcommand[0] = STRjobs;
		if (eq(varval(STRlistjobs), STRlong))
		    jobcommand[1] = STRml;
		else
		    jobcommand[1] = NULL;
		jobcommand[2] = NULL;

		dojobs(jobcommand, NULL);
		(void) pprint(pp, SHELLDIR);
	    }
	    else
		(void) pprint(pp, AREASON | SHELLDIR);
	}
	else
	    (void) pprint(pp, AREASON | SHELLDIR);
    }
    if ((jobflags & (PINTERRUPTED | PSTOPPED)) && setintr &&
	(!gointr || !eq(gointr, STRminus))) {
	if ((jobflags & PSTOPPED) == 0)
	    pflush(pp);
	pintr1(0);
	/* NOTREACHED */
    }
    reason = 0;
    fp = pp;
    do {
	/* In case of pipelines only the result of the last
	 * command should be taken in account */
	if (!anyerror && !(fp->p_flags & PBRACE)
		&& ((fp->p_flags & PPOU) || (fp->p_flags & PBACKQ)))
	    continue;
	if (fp->p_reason)
	    reason = fp->p_flags & (PSIGNALED | PINTERRUPTED) ?
		fp->p_reason | META : fp->p_reason;
    } while ((fp = fp->p_friends) != pp);
    /*
     * Don't report on backquoted jobs, cause it will mess up 
     * their output.
     */
    if ((reason != 0) && (adrof(STRprintexitvalue)) && 
	(pp->p_flags & PBACKQ) == 0)
	xprintf(CGETS(17, 2, "Exit %d\n"), reason);
    reason_str = putn((tcsh_number_t)reason);
    cleanup_push(reason_str, xfree);
    setv(STRstatus, reason_str, VAR_READWRITE);
    cleanup_ignore(reason_str);
    cleanup_until(reason_str);
    if (reason && exiterr)
	exitstat();
    pflush(pp);
}

/*
 * dowait - wait for all processes to finish
 */

/*ARGSUSED*/
void
dowait(Char **v, struct command *c)
{
    struct process *pp;

    /* the current block mask to be able to restore */
    sigset_t old_mask;

    /* block mask for critical section: OLD_MASK U {SIGCHLD} */
    sigset_t block_mask;

    /* ignore those during blocking sigsuspend:
       OLD_MASK / {SIGCHLD, possibly(SIGINT)} */
    sigset_t pause_mask;

    int opintr_disabled, gotsig;

    USE(c);
    USE(v);
    pjobs++;

    sigprocmask(SIG_BLOCK, NULL, &pause_mask);
    sigdelset(&pause_mask, SIGCHLD);
    if (setintr)
	sigdelset(&pause_mask, SIGINT);

    /* critical section, block also SIGCHLD */
    sigprocmask(SIG_BLOCK, NULL, &block_mask);
    sigaddset(&block_mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block_mask, &old_mask);

    /* detect older SIGCHLDs and remove PRUNNING flag from proclist */
    (void)handle_pending_signals();

loop:
    for (pp = proclist.p_next; pp; pp = pp->p_next)
	if (pp->p_procid &&	/* pp->p_procid == pp->p_jobid && */
	    pp->p_flags & PRUNNING) {
	    /* wait for (or pick up alredy blocked) SIGCHLD */
	    sigsuspend(&pause_mask);

	    /* make the 'wait' interuptable by CTRL-C */
	    opintr_disabled = pintr_disabled;
	    pintr_disabled = 0;
	    gotsig = handle_pending_signals();
	    pintr_disabled = opintr_disabled;
	    if (gotsig)
		break;
	    goto loop;
	}
    pjobs = 0;

    sigprocmask(SIG_SETMASK, &old_mask, NULL);
}

/*
 * pflushall - flush all jobs from list (e.g. at fork())
 */
static void
pflushall(void)
{
    struct process *pp;

    for (pp = proclist.p_next; pp != NULL; pp = pp->p_next)
	if (pp->p_procid)
	    pflush(pp);
}

/*
 * pflush - flag all process structures in the same job as the
 *	the argument process for deletion.  The actual free of the
 *	space is not done here since pflush is called at interrupt level.
 */
static void
pflush(struct process *pp)
{
    struct process *np;
    int idx;

    if (pp->p_procid == 0) {
	xprintf("%s", CGETS(17, 3, "BUG: process flushed twice"));
	return;
    }
    while (pp->p_procid != pp->p_jobid)
	pp = pp->p_friends;
    pclrcurr(pp);
    if (pp == pcurrjob)
	pcurrjob = 0;
    idx = pp->p_index;
    np = pp;
    do {
	np->p_index = np->p_procid = 0;
	np->p_flags &= ~PNEEDNOTE;
    } while ((np = np->p_friends) != pp);
    if (idx == pmaxindex) {
	for (np = proclist.p_next, idx = 0; np; np = np->p_next)
	    if (np->p_index > idx)
		idx = np->p_index;
	pmaxindex = idx;
    }
}

/*
 * pclrcurr - make sure the given job is not the current or previous job;
 *	pp MUST be the job leader
 */
static void
pclrcurr(struct process *pp)
{
    if (pp == pcurrent) {
	if (pprevious != NULL) {
	    pcurrent = pprevious;
	    pprevious = pgetcurr(pp);
	}
	else {
	    pcurrent = pgetcurr(pp);
	    pprevious = pgetcurr(pp);
	}
    }
    else if (pp == pprevious)
	pprevious = pgetcurr(pp);
}

/* +4 here is 1 for '\0', 1 ea for << >& >> */
static Char *cmdstr;
static size_t cmdmax;
static size_t cmdlen;
static Char *cmdp;
#define CMD_INIT 1024
#define CMD_INCR 64

static void
morecommand(size_t s)
{
    Char *ncmdstr;
    ptrdiff_t d;

    cmdmax += s;
    ncmdstr = xrealloc(cmdstr, cmdmax * sizeof(*cmdstr));
    d = ncmdstr - cmdstr;
    cmdstr = ncmdstr;
    cmdp += d;
}

/* GrP
 * unparse - Export padd() functionality 
 */
Char *
unparse(struct command *t)
{
    if (cmdmax == 0)
	morecommand(CMD_INIT);
    cmdp = cmdstr;
    cmdlen = 0;
    padd(t);
    *cmdp++ = '\0';
    return Strsave(cmdstr);
}


/*
 * palloc - allocate a process structure and fill it up.
 *	an important assumption is made that the process is running.
 */
void
palloc(pid_t pid, struct command *t)
{
    struct process *pp;
    int     i;

    pp = xcalloc(1, sizeof(struct process));
    pp->p_procid = pid;
    pp->p_parentid = shpgrp;
    pp->p_flags = ((t->t_dflg & F_AMPERSAND) ? 0 : PFOREGND) | PRUNNING;
    if (t->t_dflg & F_TIME)
	pp->p_flags |= PPTIME;
    if (t->t_dflg & F_BACKQ)
	pp->p_flags |= PBACKQ;
    if (t->t_dflg & F_HUP)
	pp->p_flags |= PHUP;
    if (t->t_dcom && t->t_dcom[0] && (*t->t_dcom[0] == '{'))
	pp->p_flags |= PBRACE;
    if (cmdmax == 0)
	morecommand(CMD_INIT);
    cmdp = cmdstr;
    cmdlen = 0;
    padd(t);
    *cmdp++ = 0;
    if (t->t_dflg & F_PIPEOUT) {
	pp->p_flags |= PPOU;
	if (t->t_dflg & F_STDERR)
	    pp->p_flags |= PDIAG;
    }
    pp->p_command = Strsave(cmdstr);
    if (pcurrjob) {
	struct process *fp;

	/* careful here with interrupt level */
	pp->p_cwd = 0;
	pp->p_index = pcurrjob->p_index;
	pp->p_friends = pcurrjob;
	pp->p_jobid = pcurrjob->p_procid;
	for (fp = pcurrjob; fp->p_friends != pcurrjob; fp = fp->p_friends)
	    continue;
	fp->p_friends = pp;
    }
    else {
	pcurrjob = pp;
	pp->p_jobid = pid;
	pp->p_friends = pp;
	pp->p_cwd = dcwd;
	dcwd->di_count++;
	if (pmaxindex < BIGINDEX)
	    pp->p_index = ++pmaxindex;
	else {
	    struct process *np;

	    for (i = 1;; i++) {
		for (np = proclist.p_next; np; np = np->p_next)
		    if (np->p_index == i)
			goto tryagain;
		pp->p_index = i;
		if (i > pmaxindex)
		    pmaxindex = i;
		break;
	tryagain:;
	    }
	}
	if (pcurrent == NULL)
	    pcurrent = pp;
	else if (pprevious == NULL)
	    pprevious = pp;
    }
    pp->p_next = proclist.p_next;
    proclist.p_next = pp;
#ifdef BSDTIMES
    (void) gettimeofday(&pp->p_btime, NULL);
#else /* !BSDTIMES */
# ifdef _SEQUENT_
    (void) get_process_stats(&pp->p_btime, PS_SELF, NULL, NULL);
# else /* !_SEQUENT_ */
    {
	struct tms tmptimes;

	pp->p_btime = times(&tmptimes);
    }
# endif /* !_SEQUENT_ */
#endif /* !BSDTIMES */
}

static void
padd(struct command *t)
{
    Char  **argp;

    if (t == 0)
	return;
    switch (t->t_dtyp) {

    case NODE_PAREN:
	pads(STRLparensp);
	padd(t->t_dspr);
	pads(STRspRparen);
	break;

    case NODE_COMMAND:
	for (argp = t->t_dcom; *argp; argp++) {
	    pads(*argp);
	    if (argp[1])
		pads(STRspace);
	}
	break;

    case NODE_OR:
    case NODE_AND:
    case NODE_PIPE:
    case NODE_LIST:
	padd(t->t_dcar);
	switch (t->t_dtyp) {
	case NODE_OR:
	    pads(STRspor2sp);
	    break;
	case NODE_AND:
	    pads(STRspand2sp);
	    break;
	case NODE_PIPE:
	    pads(STRsporsp);
	    break;
	case NODE_LIST:
	    pads(STRsemisp);
	    break;
	default:
	    break;
	}
	padd(t->t_dcdr);
	return;

    default:
	break;
    }
    if ((t->t_dflg & F_PIPEIN) == 0 && t->t_dlef) {
	pads((t->t_dflg & F_READ) ? STRspLarrow2sp : STRspLarrowsp);
	pads(t->t_dlef);
    }
    if ((t->t_dflg & F_PIPEOUT) == 0 && t->t_drit) {
	pads((t->t_dflg & F_APPEND) ? STRspRarrow2 : STRspRarrow);
	if (t->t_dflg & F_STDERR)
	    pads(STRand);
	pads(STRspace);
	pads(t->t_drit);
    }
}

static void
pads(Char *cp)
{
    size_t i, len;

    /*
     * Avoid the Quoted Space alias hack! Reported by:
     * sam@john-bigboote.ICS.UCI.EDU (Sam Horrocks)
     */
    if (cp[0] == STRQNULL[0])
	cp++;

    i = Strlen(cp);

    len = cmdlen + i + CMD_INCR;
    if (len >= cmdmax)
	morecommand(len);
    (void) Strcpy(cmdp, cp);
    cmdp += i;
    cmdlen += i;
}

/*
 * psavejob - temporarily save the current job on a one level stack
 *	so another job can be created.  Used for { } in exp6
 *	and `` in globbing.
 */
void
psavejob(void)
{
    pholdjob = pcurrjob;
    pcurrjob = NULL;
}

void
psavejob_cleanup(void *dummy)
{
    USE(dummy);
    pcurrjob = pholdjob;
    pholdjob = NULL;
}

/*
 * pendjob - indicate that a job (set of commands) has been completed
 *	or is about to begin.
 */
void
pendjob(void)
{
    struct process *pp, *tp;

    if (pcurrjob && (pcurrjob->p_flags & (PFOREGND | PSTOPPED)) == 0) {
	pp = pcurrjob;
	pcurrjob = NULL;
	while (pp->p_procid != pp->p_jobid)
	    pp = pp->p_friends;
	xprintf("[%d]", pp->p_index);
	tp = pp;
	do {
	    xprintf(" %d", pp->p_procid);
	    pp = pp->p_friends;
	} while (pp != tp);
	xputchar('\n');
    }
    pholdjob = pcurrjob = 0;
}

/*
 * pprint - print a job
 */

/*
 * Hacks have been added for SVR4 to deal with pipe's being spawned in
 * reverse order
 *
 * David Dawes (dawes@physics.su.oz.au) Oct 1991
 */

static int
pprint(struct process *pp, int flag)
{
    int status, reason;
    struct process *tp;
    int     jobflags, pstatus, pcond;
    const char *format;
    int ohaderr;

#ifdef BACKPIPE
    struct process *pipehead = NULL, *pipetail = NULL, *pmarker = NULL;
    int inpipe = 0;
#endif /* BACKPIPE */

    while (pp->p_procid != pp->p_jobid)
	pp = pp->p_friends;
    if (pp == pp->p_friends && (pp->p_flags & PPTIME)) {
	pp->p_flags &= ~PPTIME;
	pp->p_flags |= PTIME;
    }
    tp = pp;
    status = reason = -1;
    jobflags = 0;
    ohaderr = haderr;
    /* Print status to stderr, except for jobs built-in */
    haderr = !(flag & JOBLIST);
    do {
#ifdef BACKPIPE
	/*
	 * The pipeline is reversed, so locate the real head of the pipeline
	 * if pp is at the tail of a pipe (and not already in a pipeline)
	 */
	if ((pp->p_friends->p_flags & PPOU) && !inpipe && (flag & NAME)) {
	    inpipe = 1;
	    pipetail = pp;
	    do 
		pp = pp->p_friends;
	    while (pp->p_friends->p_flags & PPOU);
	    pipehead = pp;
	    pmarker = pp;
	/*
	 * pmarker is used to hold the place of the proc being processed, so
	 * we can search for the next one downstream later.
	 */
	}
	pcond = (tp != pp || (inpipe && tp == pp));
#else /* !BACKPIPE */
	pcond = (tp != pp);
#endif /* BACKPIPE */	    

	jobflags |= pp->p_flags;
	pstatus = (int) (pp->p_flags & PALLSTATES);
	if (pcond && linp != linbuf && !(flag & FANCY) &&
	    ((pstatus == status && pp->p_reason == reason) ||
	     !(flag & REASON)))
	    xputchar(' ');
	else {
	    if (pcond && linp != linbuf)
		xputchar('\n');
	    if (flag & NUMBER) {
#ifdef BACKPIPE
		pcond = ((pp == tp && !inpipe) ||
			 (inpipe && pipetail == tp && pp == pipehead));
#else /* BACKPIPE */
		pcond = (pp == tp);
#endif /* BACKPIPE */
		if (pcond)
		    xprintf("[%d]%s %c ", pp->p_index,
			    pp->p_index < 10 ? " " : "",
			    pp == pcurrent ? '+' :
			    (pp == pprevious ? '-' : ' '));
		else
		    xprintf("       ");
	    }
	    if (flag & FANCY) {
		xprintf("%5d ", pp->p_procid);
#ifdef TCF
		xprintf("%11s ", sitename(pp->p_procid));
#endif /* TCF */
	    }
	    if (flag & (REASON | AREASON)) {
		if (flag & NAME)
		    format = "%-30s";
		else
		    format = "%s";
		if (pstatus == status) {
		    if (pp->p_reason == reason) {
			xprintf(format, "");
			goto prcomd;
		    }
		    else
			reason = (int) pp->p_reason;
		}
		else {
		    status = pstatus;
		    reason = (int) pp->p_reason;
		}
		switch (status) {

		case PRUNNING:
		    xprintf(format, CGETS(17, 4, "Running "));
		    break;

		case PINTERRUPTED:
		case PSTOPPED:
		case PSIGNALED:
		    /*
		     * tell what happened to the background job
		     * From: Michael Schroeder
		     * <mlschroe@immd4.informatik.uni-erlangen.de>
		     */
		    if ((flag & REASON)
			|| ((flag & AREASON)
			    && reason != SIGINT
			    && (reason != SIGPIPE
				|| (pp->p_flags & PPOU) == 0))) {
			char *ptr;
			int free_ptr;

			free_ptr = 0;
			ptr = (char *)(intptr_t)mesg[pp->p_reason & 0177].pname;
			if (ptr == NULL) {
			    ptr = xasprintf("%s %d", CGETS(17, 5, "Signal"),
					    pp->p_reason & 0177);
			    cleanup_push(ptr, xfree);
			    free_ptr = 1;
			}
			xprintf(format, ptr);
			if (free_ptr != 0)
			    cleanup_until(ptr);
		    }
		    else
			reason = -1;
		    break;

		case PNEXITED:
		case PAEXITED:
		    if (flag & REASON) {
			if (pp->p_reason)
			    xprintf(CGETS(17, 6, "Exit %-25d"), pp->p_reason);
			else
			    xprintf(format, CGETS(17, 7, "Done"));
		    }
		    break;

		default:
		    xprintf(CGETS(17, 8, "BUG: status=%-9o"),
			    status);
		}
	    }
	}
prcomd:
	if (flag & NAME) {
	    xprintf("%S", pp->p_command);
	    if (pp->p_flags & PPOU)
		xprintf(" |");
	    if (pp->p_flags & PDIAG)
		xprintf("&");
	}
	if (flag & (REASON | AREASON) && pp->p_flags & PDUMPED)
	    xprintf("%s", CGETS(17, 9, " (core dumped)"));
	if (tp == pp->p_friends) {
	    if (flag & AMPERSAND)
		xprintf(" &");
	    if (flag & JOBDIR &&
		!eq(tp->p_cwd->di_name, dcwd->di_name)) {
		xprintf("%s", CGETS(17, 10, " (wd: "));
		dtildepr(tp->p_cwd->di_name);
		xprintf(")");
	    }
	}
	if (pp->p_flags & PPTIME && !(status & (PSTOPPED | PRUNNING))) {
	    if (linp != linbuf)
		xprintf("\n\t");
#if defined(BSDTIMES) || defined(_SEQUENT_)
	    prusage(&zru, &pp->p_rusage, &pp->p_etime,
		    &pp->p_btime);
#else /* !BSDTIMES && !SEQUENT */
	    lru.tms_utime = pp->p_utime;
	    lru.tms_stime = pp->p_stime;
	    lru.tms_cutime = 0;
	    lru.tms_cstime = 0;
	    prusage(&zru, &lru, pp->p_etime,
		    pp->p_btime);
#endif /* !BSDTIMES && !SEQUENT */

	}
#ifdef BACKPIPE
	pcond = ((tp == pp->p_friends && !inpipe) ||
		 (inpipe && pipehead->p_friends == tp && pp == pipetail));
#else  /* !BACKPIPE */
	pcond = (tp == pp->p_friends);
#endif /* BACKPIPE */
	if (pcond) {
	    if (linp != linbuf)
		xputchar('\n');
	    if (flag & SHELLDIR && !eq(tp->p_cwd->di_name, dcwd->di_name)) {
		xprintf("%s", CGETS(17, 11, "(wd now: "));
		dtildepr(dcwd->di_name);
		xprintf(")\n");
	    }
	}
#ifdef BACKPIPE
	if (inpipe) {
	    /*
	     * if pmaker == pipetail, we are finished that pipeline, and
	     * can now skip to past the head
	     */
	    if (pmarker == pipetail) {
		inpipe = 0;
		pp = pipehead;
	    }
	    else {
	    /*
	     * set pp to one before the one we want next, so the while below
	     * increments to the correct spot.
	     */
		do
		    pp = pp->p_friends;
	    	while (pp->p_friends->p_friends != pmarker);
	    	pmarker = pp->p_friends;
	    }
	}
	pcond = ((pp = pp->p_friends) != tp || inpipe);
#else /* !BACKPIPE */
	pcond = ((pp = pp->p_friends) != tp);
#endif /* BACKPIPE */
    } while (pcond);

    if (jobflags & PTIME && (jobflags & (PSTOPPED | PRUNNING)) == 0) {
	if (jobflags & NUMBER)
	    xprintf("       ");
	ptprint(tp);
    }
    haderr = ohaderr;
    return (jobflags);
}

/*
 * All 4.3 BSD derived implementations are buggy and I've had enough.
 * The following implementation produces similar code and works in all
 * cases. The 4.3BSD one works only for <, >, !=
 */
# undef timercmp
#  define timercmp(tvp, uvp, cmp) \
      (((tvp)->tv_sec == (uvp)->tv_sec) ? \
	   ((tvp)->tv_usec cmp (uvp)->tv_usec) : \
	   ((tvp)->tv_sec  cmp (uvp)->tv_sec))

static void
ptprint(struct process *tp)
{
#ifdef BSDTIMES
    struct timeval tetime, diff;
    static struct timeval ztime;
    struct sysrusage ru;
    struct process *pp = tp;

    ru = zru;
    tetime = ztime;
    do {
	ruadd(&ru, &pp->p_rusage);
	tvsub(&diff, &pp->p_etime, &pp->p_btime);
	if (timercmp(&diff, &tetime, >))
	    tetime = diff;
    } while ((pp = pp->p_friends) != tp);
    prusage(&zru, &ru, &tetime, &ztime);
#else /* !BSDTIMES */
# ifdef _SEQUENT_
    timeval_t tetime, diff;
    static timeval_t ztime;
    struct process_stats ru;
    struct process *pp = tp;

    ru = zru;
    tetime = ztime;
    do {
	ruadd(&ru, &pp->p_rusage);
	tvsub(&diff, &pp->p_etime, &pp->p_btime);
	if (timercmp(&diff, &tetime, >))
	    tetime = diff;
    } while ((pp = pp->p_friends) != tp);
    prusage(&zru, &ru, &tetime, &ztime);
# else /* !_SEQUENT_ */
#  ifndef POSIX
    static time_t ztime = 0;
    static time_t zu_time = 0;
    static time_t zs_time = 0;
    time_t  tetime, diff;
    time_t  u_time, s_time;

#  else	/* POSIX */
    static clock_t ztime = 0;
    static clock_t zu_time = 0;
    static clock_t zs_time = 0;
    clock_t tetime, diff;
    clock_t u_time, s_time;

#  endif /* POSIX */
    struct tms zts, rts;
    struct process *pp = tp;

    u_time = zu_time;
    s_time = zs_time;
    tetime = ztime;
    do {
	u_time += pp->p_utime;
	s_time += pp->p_stime;
	diff = pp->p_etime - pp->p_btime;
	if (diff > tetime)
	    tetime = diff;
    } while ((pp = pp->p_friends) != tp);
    zts.tms_utime = zu_time;
    zts.tms_stime = zs_time;
    zts.tms_cutime = 0;
    zts.tms_cstime = 0;
    rts.tms_utime = u_time;
    rts.tms_stime = s_time;
    rts.tms_cutime = 0;
    rts.tms_cstime = 0;
    prusage(&zts, &rts, tetime, ztime);
# endif /* !_SEQUENT_ */
#endif	/* !BSDTIMES */
}

/*
 * dojobs - print all jobs
 */
/*ARGSUSED*/
void
dojobs(Char **v, struct command *c)
{
    struct process *pp;
    int flag = NUMBER | NAME | REASON | JOBLIST;
    int     i;

    USE(c);
    if (chkstop)
	chkstop = 2;
    if (*++v) {
	if (v[1] || !eq(*v, STRml))
	    stderror(ERR_JOBS);
	flag |= FANCY | JOBDIR;
    }
    for (i = 1; i <= pmaxindex; i++)
	for (pp = proclist.p_next; pp; pp = pp->p_next)
	    if (pp->p_index == i && pp->p_procid == pp->p_jobid) {
		pp->p_flags &= ~PNEEDNOTE;
		if (!(pprint(pp, flag) & (PRUNNING | PSTOPPED)))
		    pflush(pp);
		break;
	    }
}

/*
 * dofg - builtin - put the job into the foreground
 */
/*ARGSUSED*/
void
dofg(Char **v, struct command *c)
{
    struct process *pp;

    USE(c);
    okpcntl();
    ++v;
    do {
	pp = pfind(*v);
	if (!pstart(pp, 1)) {
	    pp->p_procid = 0;
	    stderror(ERR_NAME|ERR_BADJOB, pp->p_command, strerror(errno));
	    continue;
	}
	pjwait(pp);
    } while (*v && *++v);
}

/*
 * %... - builtin - put the job into the foreground
 */
/*ARGSUSED*/
void
dofg1(Char **v, struct command *c)
{
    struct process *pp;

    USE(c);
    okpcntl();
    pp = pfind(v[0]);
    if (!pstart(pp, 1)) {
	pp->p_procid = 0;
	stderror(ERR_NAME|ERR_BADJOB, pp->p_command, strerror(errno));
	return;
    }
    pjwait(pp);
}

/*
 * dobg - builtin - put the job into the background
 */
/*ARGSUSED*/
void
dobg(Char **v, struct command *c)
{
    struct process *pp;

    USE(c);
    okpcntl();
    ++v;
    do {
	pp = pfind(*v);
	if (!pstart(pp, 0)) {
	    pp->p_procid = 0;
	    stderror(ERR_NAME|ERR_BADJOB, pp->p_command, strerror(errno));
	}
    } while (*v && *++v);
}

/*
 * %... & - builtin - put the job into the background
 */
/*ARGSUSED*/
void
dobg1(Char **v, struct command *c)
{
    struct process *pp;

    USE(c);
    pp = pfind(v[0]);
    if (!pstart(pp, 0)) {
	pp->p_procid = 0;
	stderror(ERR_NAME|ERR_BADJOB, pp->p_command, strerror(errno));
    }
}

/*
 * dostop - builtin - stop the job
 */
/*ARGSUSED*/
void
dostop(Char **v, struct command *c)
{
    USE(c);
#ifdef BSDJOBS
    pkill(++v, SIGSTOP);
#endif /* BSDJOBS */
}

/*
 * dokill - builtin - superset of kill (1)
 */
/*ARGSUSED*/
void
dokill(Char **v, struct command *c)
{
    int signum, len = 0;
    const char *name;
    Char *sigptr;

    USE(c);
    v++;
    if (v[0] && v[0][0] == '-') {
	if (v[0][1] == 'l') {
	    for (signum = 0; signum <= nsig; signum++) {
		if ((name = mesg[signum].iname) != NULL) {
		    len += strlen(name) + 1;
		    if (len >= TermH - 1) {
			xputchar('\n');
			len = strlen(name) + 1;
		    }
		    xprintf("%s ", name);
		}
	    }
	    xputchar('\n');
	    return;
	}
 	sigptr = &v[0][1];
 	if (v[0][1] == 's') {
 	    if (v[1]) {
 		v++;
 		sigptr = &v[0][0];
 	    } else {
 		stderror(ERR_NAME | ERR_TOOFEW);
 	    }
 	}
 	if (Isdigit(*sigptr)) {
	    char *ep;
 	    signum = strtoul(short2str(sigptr), &ep, 0);
	    if (*ep || signum < 0 || signum > (MAXSIG-1))
		stderror(ERR_NAME | ERR_BADSIG);
	}
	else {
	    for (signum = 0; signum <= nsig; signum++)
		if (mesg[signum].iname &&
 		    eq(sigptr, str2short(mesg[signum].iname)))
		    goto gotsig;
 	    setname(short2str(sigptr));
	    stderror(ERR_NAME | ERR_UNKSIG);
	}
gotsig:
	v++;
    }
    else
	signum = SIGTERM;
    pkill(v, signum);
}

static void
pkill(Char **v, int signum)
{
    struct process *pp, *np;
    int jobflags = 0, err1 = 0;
    pid_t     pid;
    Char *cp, **vp, **globbed;

    /* Avoid globbing %?x patterns */
    for (vp = v; vp && *vp; vp++)
	if (**vp == '%')
	    (void) quote(*vp);

    v = glob_all_or_error(v);
    globbed = v;
    cleanup_push(globbed, blk_cleanup);

    pchild_disabled++;
    cleanup_push(&pchild_disabled, disabled_cleanup);
    if (setintr) {
	pintr_disabled++;
	cleanup_push(&pintr_disabled, disabled_cleanup);
    }

    while (v && (cp = *v)) {
	if (*cp == '%') {
	    np = pp = pfind(cp);
	    do
		jobflags |= np->p_flags;
	    while ((np = np->p_friends) != pp);
#ifdef BSDJOBS
	    switch (signum) {

	    case SIGSTOP:
	    case SIGTSTP:
	    case SIGTTIN:
	    case SIGTTOU:
		if ((jobflags & PRUNNING) == 0) {
# ifdef SUSPENDED
		    xprintf(CGETS(17, 12, "%S: Already suspended\n"), cp);
# else /* !SUSPENDED */
		    xprintf(CGETS(17, 13, "%S: Already stopped\n"), cp);
# endif /* !SUSPENDED */
		    err1++;
		    goto cont;
		}
		break;
		/*
		 * suspend a process, kill -CONT %, then type jobs; the shell
		 * says it is suspended, but it is running; thanks jaap..
		 */
	    case SIGCONT:
		if (!pstart(pp, 0)) {
		    pp->p_procid = 0;
		    stderror(ERR_NAME|ERR_BADJOB, pp->p_command,
			     strerror(errno));
		}
		goto cont;
	    default:
		break;
	    }
#endif /* BSDJOBS */
	    if (killpg(pp->p_jobid, signum) < 0) {
		xprintf("%S: %s\n", cp, strerror(errno));
		err1++;
	    }
#ifdef BSDJOBS
	    if (signum == SIGTERM || signum == SIGHUP)
		(void) killpg(pp->p_jobid, SIGCONT);
#endif /* BSDJOBS */
	}
	else if (!(Isdigit(*cp) || *cp == '-'))
	    stderror(ERR_NAME | ERR_JOBARGS);
	else {
	    char *ep;
#ifndef WINNT_NATIVE
	    pid = strtol(short2str(cp), &ep, 10);
#else
	    pid = strtoul(short2str(cp), &ep, 0);
#endif /* WINNT_NATIVE */
	    if (*ep)
		stderror(ERR_NAME | ERR_JOBARGS);
	    else if (kill(pid, signum) < 0) {
		xprintf("%d: %s\n", pid, strerror(errno));
		err1++;
		goto cont;
	    }
#ifdef BSDJOBS
	    if (signum == SIGTERM || signum == SIGHUP)
		(void) kill(pid, SIGCONT);
#endif /* BSDJOBS */
	}
cont:
	v++;
    }
    cleanup_until(&pchild_disabled);
    if (err1)
	stderror(ERR_SILENT);
}

/*
 * pstart - start the job in foreground/background
 */
int
pstart(struct process *pp, int foregnd)
{
    int rv = 0;
    struct process *np;
    /* We don't use jobflags in this function right now (see below) */
    /* long    jobflags = 0; */

    pchild_disabled++;
    cleanup_push(&pchild_disabled, disabled_cleanup);
    np = pp;
    do {
	/* We don't use jobflags in this function right now (see below) */
	/* jobflags |= np->p_flags; */
	if (np->p_flags & (PRUNNING | PSTOPPED)) {
	    np->p_flags |= PRUNNING;
	    np->p_flags &= ~PSTOPPED;
	    if (foregnd)
		np->p_flags |= PFOREGND;
	    else
		np->p_flags &= ~PFOREGND;
	}
    } while ((np = np->p_friends) != pp);
    if (!foregnd)
	pclrcurr(pp);
    (void) pprint(pp, foregnd ? NAME | JOBDIR : NUMBER | NAME | AMPERSAND);

    /* GrP run jobcmd hook if foregrounding */
    if (foregnd) {
	job_cmd(pp->p_command);
    }

#ifdef BSDJOBS
    if (foregnd) {
	rv = tcsetpgrp(FSHTTY, pp->p_jobid);
    }
    /*
     * 1. child process of csh (shell script) receives SIGTTIN/SIGTTOU
     * 2. parent process (csh) receives SIGCHLD
     * 3. The "csh" signal handling function pchild() is invoked
     *    with a SIGCHLD signal.
     * 4. pchild() calls wait3(WNOHANG) which returns 0.
     *    The child process is NOT ready to be waited for at this time.
     *    pchild() returns without picking-up the correct status
     *    for the child process which generated the SIGCHLD.
     * 5. CONSEQUENCE : csh is UNaware that the process is stopped
     * 6. THIS LINE HAS BEEN COMMENTED OUT : if (jobflags&PSTOPPED)
     * 	  (beto@aixwiz.austin.ibm.com - aug/03/91)
     * 7. I removed the line completely and added extra checks for
     *    pstart, so that if a job gets attached to and dies inside
     *    a debugger it does not confuse the shell. [christos]
     * 8. on the nec sx-4 there seems to be a problem, which requires
     *    a syscall(151, getpid(), getpid()) in osinit. Don't ask me
     *    what this is doing. [schott@rzg.mpg.de]
     */

    if (rv != -1)
	rv = killpg(pp->p_jobid, SIGCONT);
#endif /* BSDJOBS */
    cleanup_until(&pchild_disabled);
    return rv != -1;
}

void
panystop(int neednl)
{
    struct process *pp;

    chkstop = 2;
    for (pp = proclist.p_next; pp; pp = pp->p_next)
	if (pp->p_flags & PSTOPPED)
	    stderror(ERR_STOPPED, neednl ? "\n" : "");
}

struct process *
pfind(Char *cp)
{
    struct process *pp, *np;

    if (cp == 0 || cp[1] == 0 || eq(cp, STRcent2) || eq(cp, STRcentplus)) {
	if (pcurrent == NULL)
	    stderror(ERR_NAME | ERR_JOBCUR);
	return (pcurrent);
    }
    if (eq(cp, STRcentminus) || eq(cp, STRcenthash)) {
	if (pprevious == NULL)
	    stderror(ERR_NAME | ERR_JOBPREV);
	return (pprevious);
    }
    if (Isdigit(cp[1])) {
	int     idx = atoi(short2str(cp + 1));

	for (pp = proclist.p_next; pp; pp = pp->p_next)
	    if (pp->p_index == idx && pp->p_procid == pp->p_jobid)
		return (pp);
	stderror(ERR_NAME | ERR_NOSUCHJOB);
    }
    np = NULL;
    for (pp = proclist.p_next; pp; pp = pp->p_next)
	if (pp->p_procid == pp->p_jobid) {
	    if (cp[1] == '?') {
		Char *dp;

		for (dp = pp->p_command; *dp; dp++) {
		    if (*dp != cp[2])
			continue;
		    if (prefix(cp + 2, dp))
			goto match;
		}
	    }
	    else if (prefix(cp + 1, pp->p_command)) {
	match:
		if (np)
		    stderror(ERR_NAME | ERR_AMBIG);
		np = pp;
	    }
	}
    if (np)
	return (np);
    stderror(ERR_NAME | (cp[1] == '?' ? ERR_JOBPAT : ERR_NOSUCHJOB));
    /* NOTREACHED */
    return (0);
}


/*
 * pgetcurr - find most recent job that is not pp, preferably stopped
 */
static struct process *
pgetcurr(struct process *pp)
{
    struct process *np;
    struct process *xp = NULL;

    for (np = proclist.p_next; np; np = np->p_next)
	if (np != pcurrent && np != pp && np->p_procid &&
	    np->p_procid == np->p_jobid) {
	    if (np->p_flags & PSTOPPED)
		return (np);
	    if (xp == NULL)
		xp = np;
	}
    return (xp);
}

/*
 * donotify - flag the job so as to report termination asynchronously
 */
/*ARGSUSED*/
void
donotify(Char **v, struct command *c)
{
    struct process *pp;

    USE(c);
    pp = pfind(*++v);
    pp->p_flags |= PNOTIFY;
}

#ifdef SIGSYNCH
static void
synch_handler(int sno)
{
    USE(sno);
}
#endif /* SIGSYNCH */

/*
 * Do the fork and whatever should be done in the child side that
 * should not be done if we are not forking at all (like for simple builtin's)
 * Also do everything that needs any signals fiddled with in the parent side
 *
 * Wanttty tells whether process and/or tty pgrps are to be manipulated:
 *	-1:	leave tty alone; inherit pgrp from parent
 *	 0:	already have tty; manipulate process pgrps only
 *	 1:	want to claim tty; manipulate process and tty pgrps
 * It is usually just the value of tpgrp.
 */

pid_t
pfork(struct command *t, int wanttty)
{
    pid_t pid;
    int    ignint = 0;
    pid_t pgrp;
#ifdef SIGSYNCH
    struct sigaction osa, nsa;
#endif /* SIGSYNCH */

    /*
     * A child will be uninterruptible only under very special conditions.
     * Remember that the semantics of '&' is implemented by disconnecting the
     * process from the tty so signals do not need to ignored just for '&'.
     * Thus signals are set to default action for children unless: we have had
     * an "onintr -" (then specifically ignored) we are not playing with
     * signals (inherit action)
     */
    if (setintr)
	ignint = (tpgrp == -1 && (t->t_dflg & F_NOINTERRUPT))
	    || (gointr && eq(gointr, STRminus));

    /*
     * Check for maximum nesting of 16 processes to avoid Forking loops
     */
    if (child == 16)
	stderror(ERR_NESTING, 16);
#ifdef SIGSYNCH
    nsa.sa_handler = synch_handler;
    sigfillset(&nsa.sa_mask);
    nsa.sa_flags = SA_RESTART;
    if (sigaction(SIGSYNCH, &nsa, &osa))
	stderror(ERR_SYSTEM, "pfork: sigaction set", strerror(errno));
#endif /* SIGSYNCH */
    /*
     * Hold pchild() until we have the process installed in our table.
     */
    if (wanttty < 0) {
	pchild_disabled++;
	cleanup_push(&pchild_disabled, disabled_cleanup);
    }
    while ((pid = fork()) == -1)
	if (setintr == 0)
	    (void) sleep(FORKSLEEP);
	else
	    stderror(ERR_NOPROC);
    if (pid == 0) {
	(void)cleanup_push_mark(); /* Never to be popped */
	pchild_disabled = 0;
	settimes();
	pgrp = pcurrjob ? pcurrjob->p_jobid : getpid();
	pflushall();
	pcurrjob = NULL;
#if !defined(BSDTIMES) && !defined(_SEQUENT_) 
	timesdone = 0;
#endif /* !defined(BSDTIMES) && !defined(_SEQUENT_) */
	child++;
	if (setintr) {
	    setintr = 0;	/* until I think otherwise */
	    /*
	     * Children just get blown away on SIGINT, SIGQUIT unless "onintr
	     * -" seen.
	     */
	    (void) signal(SIGINT, ignint ? SIG_IGN : SIG_DFL);
	    (void) signal(SIGQUIT, ignint ? SIG_IGN : SIG_DFL);
#ifdef BSDJOBS
	    if (wanttty >= 0) {
		/* make stoppable */
		(void) signal(SIGTSTP, SIG_DFL);
		(void) signal(SIGTTIN, SIG_DFL);
		(void) signal(SIGTTOU, SIG_DFL);
	    }
#endif /* BSDJOBS */
	    sigaction(SIGTERM, &parterm, NULL);
	}
	else if (tpgrp == -1 && (t->t_dflg & F_NOINTERRUPT)) {
	    (void) signal(SIGINT, SIG_IGN);
	    (void) signal(SIGQUIT, SIG_IGN);
	}
#ifdef OREO
	signal(SIGIO, SIG_IGN);	/* ignore SIGIO in child too */
#endif /* OREO */

	pgetty(wanttty, pgrp);
	/*
	 * Nohup and nice apply only to NODE_COMMAND's but it would be nice
	 * (?!?) if you could say "nohup (foo;bar)" Then the parser would have
	 * to know about nice/nohup/time
	 */
	if (t->t_dflg & F_NOHUP)
	    (void) signal(SIGHUP, SIG_IGN);
	if (t->t_dflg & F_NICE) {
	    int nval = SIGN_EXTEND_CHAR(t->t_nice);
#if defined(HAVE_SETPRIORITY) && defined(PRIO_PROCESS)
	    if (setpriority(PRIO_PROCESS, 0, nval) == -1 && errno)
		stderror(ERR_SYSTEM, "setpriority", strerror(errno));
#else /* !HAVE_SETPRIORITY || !PRIO_PROCESS */
	    (void) nice(nval);
#endif /* HAVE_SETPRIORITY  && PRIO_PROCESS */
	}
#ifdef F_VER
        if (t->t_dflg & F_VER) {
	    tsetenv(STRSYSTYPE, t->t_systype ? STRbsd43 : STRsys53);
	    dohash(NULL, NULL);
	}
#endif /* F_VER */
#ifdef SIGSYNCH
	/* rfw 8/89 now parent can continue */
	if (kill(getppid(), SIGSYNCH))
	    stderror(ERR_SYSTEM, "pfork child: kill", strerror(errno));
#endif /* SIGSYNCH */

    }
    else {
#ifdef POSIXJOBS
        if (wanttty >= 0) {
	    /*
	     * `Walking' process group fix from Beto Appleton.
	     * (beto@aixwiz.austin.ibm.com)
	     * If setpgid fails at this point that means that
	     * our process leader has died. We flush the current
	     * job and become the process leader ourselves.
	     * The parent will figure that out later.
	     */
	    pgrp = pcurrjob ? pcurrjob->p_jobid : pid;
	    if (setpgid(pid, pgrp) == -1 && errno == EPERM) {
		pcurrjob = NULL;
		/* 
		 * We don't care if this causes an error here;
		 * then we are already in the right process group
		 */
		(void) setpgid(pid, pgrp = pid);
	    }
	}
#endif /* POSIXJOBS */
	palloc(pid, t);
#ifdef SIGSYNCH
	{
	    sigset_t pause_mask;

	/*
	 * rfw 8/89 Wait for child to own terminal.  Solves half of ugly
	 * synchronization problem.  With this change, we know that the only
	 * reason setpgrp to a previous process in a pipeline can fail is that
	 * the previous process has already exited. Without this hack, he may
	 * either have exited or not yet started to run.  Two uglies become
	 * one.
	 */
	    sigprocmask(SIG_BLOCK, NULL, &pause);
	    sigdelset(&pause_mask, SIGCHLD);
	    sigdelset(&pause_mask, SIGSYNCH);
	    sigsuspend(&pause_mask);
	    (void)handle_pending_signals();
	    if (sigaction(SIGSYNCH, &osa, NULL))
		stderror(ERR_SYSTEM, "pfork parent: sigaction restore",
			 strerror(errno));
	}
#endif /* SIGSYNCH */

	if (wanttty < 0)
	    cleanup_until(&pchild_disabled);
    }
    return (pid);
}

static void
okpcntl(void)
{
    if (tpgrp == -1)
	stderror(ERR_JOBCONTROL);
    if (tpgrp == 0)
	stderror(ERR_JOBCTRLSUB);
}


static void
setttypgrp(int pgrp)
{
    /*
     * If we are piping out a builtin, eg. 'echo | more' things can go
     * out of sequence, i.e. the more can run before the echo. This
     * can happen even if we have vfork, since the echo will be forked
     * with the regular fork. In this case, we need to set the tty
     * pgrp ourselves. If that happens, then the process will be still
     * alive. And the tty process group will already be set.
     * This should fix the famous sequent problem as a side effect:
     *    The controlling terminal is lost if all processes in the
     *    terminal process group are zombies. In this case tcgetpgrp()
     *    returns 0. If this happens we must set the terminal process
     *    group again.
     */
    if (tcgetpgrp(FSHTTY) != pgrp) {
#ifdef POSIXJOBS
	struct sigaction old;

        /*
	 * tcsetpgrp will set SIGTTOU to all the the processes in 
	 * the background according to POSIX... We ignore this here.
	 */
	sigaction(SIGTTOU, NULL, &old);
	signal(SIGTTOU, SIG_IGN);
#endif
	(void) tcsetpgrp(FSHTTY, pgrp);
# ifdef POSIXJOBS
	sigaction(SIGTTOU, &old, NULL);
# endif

    }
}


/*
 * if we don't have vfork(), things can still go in the wrong order
 * resulting in the famous 'Stopped (tty output)'. But some systems
 * don't permit the setpgid() call, (these are more recent secure
 * systems such as ibm's aix), when they do. Then we'd rather print 
 * an error message than hang the shell!
 * I am open to suggestions how to fix that.
 */
void
pgetty(int wanttty, pid_t pgrp)
{
#ifdef BSDJOBS
# ifdef POSIXJOBS
    sigset_t oset, set;
# endif /* POSIXJOBS */

    jobdebug_xprintf(("wanttty %d pid %d opgrp%d pgrp %d tpgrp %d\n",
		      wanttty, (int)getpid(), (int)pgrp, (int)mygetpgrp(),
		      (int)tcgetpgrp(FSHTTY)));
# ifdef POSIXJOBS
    /*
     * christos: I am blocking the tty signals till I've set things
     * correctly....
     */
    if (wanttty > 0) {
	sigemptyset(&set);
	sigaddset(&set, SIGTSTP);
	sigaddset(&set, SIGTTIN);
	(void)sigprocmask(SIG_BLOCK, &set, &oset);
	cleanup_push(&oset, sigprocmask_cleanup);
    }
# endif /* POSIXJOBS */

# ifndef POSIXJOBS
    if (wanttty > 0)
	setttypgrp(pgrp);
# endif /* !POSIXJOBS */

    /*
     * From: Michael Schroeder <mlschroe@immd4.informatik.uni-erlangen.de>
     * Don't check for tpgrp >= 0 so even non-interactive shells give
     * background jobs process groups Same for the comparison in the other part
     * of the #ifdef
     */
    if (wanttty >= 0) {
	if (setpgid(0, pgrp) == -1) {
# ifdef POSIXJOBS
	    /* Walking process group fix; see above */
	    if (setpgid(0, pgrp = getpid()) == -1) {
# endif /* POSIXJOBS */
		stderror(ERR_SYSTEM, "setpgid child:\n", strerror(errno));
		xexit(0);
# ifdef POSIXJOBS
	    }
	    wanttty = pgrp;  /* Now we really want the tty, since we became the
			      * the process group leader
			      */
# endif /* POSIXJOBS */
	}
    }

# ifdef POSIXJOBS
    if (wanttty > 0) {
	setttypgrp(pgrp);
	cleanup_until(&oset);
    }
# endif /* POSIXJOBS */

    jobdebug_xprintf(("wanttty %d pid %d pgrp %d tpgrp %d\n",
		      wanttty, getpid(), mygetpgrp(), tcgetpgrp(FSHTTY)));

    if (tpgrp > 0)
	tpgrp = 0;		/* gave tty away */
#endif /* BSDJOBS */
}
