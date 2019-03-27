/*	$NetBSD: job.c,v 1.195 2018/05/13 22:13:28 sjg Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
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

/*
 * Copyright (c) 1988, 1989 by Adam de Boor
 * Copyright (c) 1989 by Berkeley Softworks
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
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

#ifndef MAKE_NATIVE
static char rcsid[] = "$NetBSD: job.c,v 1.195 2018/05/13 22:13:28 sjg Exp $";
#else
#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)job.c	8.2 (Berkeley) 3/19/94";
#else
__RCSID("$NetBSD: job.c,v 1.195 2018/05/13 22:13:28 sjg Exp $");
#endif
#endif /* not lint */
#endif

/*-
 * job.c --
 *	handle the creation etc. of our child processes.
 *
 * Interface:
 *	Job_Make  	    	Start the creation of the given target.
 *
 *	Job_CatchChildren   	Check for and handle the termination of any
 *	    	  	    	children. This must be called reasonably
 *	    	  	    	frequently to keep the whole make going at
 *	    	  	    	a decent clip, since job table entries aren't
 *	    	  	    	removed until their process is caught this way.
 *
 *	Job_CatchOutput	    	Print any output our children have produced.
 *	    	  	    	Should also be called fairly frequently to
 *	    	  	    	keep the user informed of what's going on.
 *	    	  	    	If no output is waiting, it will block for
 *	    	  	    	a time given by the SEL_* constants, below,
 *	    	  	    	or until output is ready.
 *
 *	Job_Init  	    	Called to initialize this module. in addition,
 *	    	  	    	any commands attached to the .BEGIN target
 *	    	  	    	are executed before this function returns.
 *	    	  	    	Hence, the makefile must have been parsed
 *	    	  	    	before this function is called.
 *
 *	Job_End  	    	Cleanup any memory used.
 *
 *	Job_ParseShell	    	Given the line following a .SHELL target, parse
 *	    	  	    	the line as a shell specification. Returns
 *	    	  	    	FAILURE if the spec was incorrect.
 *
 *	Job_Finish	    	Perform any final processing which needs doing.
 *	    	  	    	This includes the execution of any commands
 *	    	  	    	which have been/were attached to the .END
 *	    	  	    	target. It should only be called when the
 *	    	  	    	job table is empty.
 *
 *	Job_AbortAll	    	Abort all currently running jobs. It doesn't
 *	    	  	    	handle output or do anything for the jobs,
 *	    	  	    	just kills them. It should only be called in
 *	    	  	    	an emergency, as it were.
 *
 *	Job_CheckCommands   	Verify that the commands for a target are
 *	    	  	    	ok. Provide them if necessary and possible.
 *
 *	Job_Touch 	    	Update a target without really updating it.
 *
 *	Job_Wait  	    	Wait for all currently-running jobs to finish.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/time.h>
#include "wait.h"

#include <assert.h>
#include <errno.h>
#if !defined(USE_SELECT) && defined(HAVE_POLL_H)
#include <poll.h>
#else
#ifndef USE_SELECT			/* no poll.h */
# define USE_SELECT
#endif
#if defined(HAVE_SYS_SELECT_H)
# include <sys/select.h>
#endif
#endif
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <utime.h>
#if defined(HAVE_SYS_SOCKET_H)
# include <sys/socket.h>
#endif

#include "make.h"
#include "hash.h"
#include "dir.h"
#include "job.h"
#include "pathnames.h"
#include "trace.h"
# define STATIC static

/*
 * FreeBSD: traditionally .MAKE is not required to
 * pass jobs queue to sub-makes.
 * Use .MAKE.ALWAYS_PASS_JOB_QUEUE=no to disable.
 */
#define MAKE_ALWAYS_PASS_JOB_QUEUE ".MAKE.ALWAYS_PASS_JOB_QUEUE"
static int Always_pass_job_queue = TRUE;
/*
 * FreeBSD: aborting entire parallel make isn't always
 * desired. When doing tinderbox for example, failure of
 * one architecture should not stop all.
 * We still want to bail on interrupt though.
 */
#define MAKE_JOB_ERROR_TOKEN "MAKE_JOB_ERROR_TOKEN"
static int Job_error_token = TRUE;

/*
 * error handling variables
 */
static int     	errors = 0;	    /* number of errors reported */
static int    	aborting = 0;	    /* why is the make aborting? */
#define ABORT_ERROR	1   	    /* Because of an error */
#define ABORT_INTERRUPT	2   	    /* Because it was interrupted */
#define ABORT_WAIT	3   	    /* Waiting for jobs to finish */
#define JOB_TOKENS	"+EI+"	    /* Token to requeue for each abort state */

/*
 * this tracks the number of tokens currently "out" to build jobs.
 */
int jobTokensRunning = 0;
int not_parallel = 0;		    /* set if .NOT_PARALLEL */

/*
 * XXX: Avoid SunOS bug... FILENO() is fp->_file, and file
 * is a char! So when we go above 127 we turn negative!
 */
#define FILENO(a) ((unsigned) fileno(a))

/*
 * post-make command processing. The node postCommands is really just the
 * .END target but we keep it around to avoid having to search for it
 * all the time.
 */
static GNode   	  *postCommands = NULL;
				    /* node containing commands to execute when
				     * everything else is done */
static int     	  numCommands; 	    /* The number of commands actually printed
				     * for a target. Should this number be
				     * 0, no shell will be executed. */

/*
 * Return values from JobStart.
 */
#define JOB_RUNNING	0   	/* Job is running */
#define JOB_ERROR 	1   	/* Error in starting the job */
#define JOB_FINISHED	2   	/* The job is already finished */

/*
 * Descriptions for various shells.
 *
 * The build environment may set DEFSHELL_INDEX to one of
 * DEFSHELL_INDEX_SH, DEFSHELL_INDEX_KSH, or DEFSHELL_INDEX_CSH, to
 * select one of the prefedined shells as the default shell.
 *
 * Alternatively, the build environment may set DEFSHELL_CUSTOM to the
 * name or the full path of a sh-compatible shell, which will be used as
 * the default shell.
 *
 * ".SHELL" lines in Makefiles can choose the default shell from the
 # set defined here, or add additional shells.
 */

#ifdef DEFSHELL_CUSTOM
#define DEFSHELL_INDEX_CUSTOM 0
#define DEFSHELL_INDEX_SH     1
#define DEFSHELL_INDEX_KSH    2
#define DEFSHELL_INDEX_CSH    3
#else /* !DEFSHELL_CUSTOM */
#define DEFSHELL_INDEX_SH     0
#define DEFSHELL_INDEX_KSH    1
#define DEFSHELL_INDEX_CSH    2
#endif /* !DEFSHELL_CUSTOM */

#ifndef DEFSHELL_INDEX
#define DEFSHELL_INDEX 0	/* DEFSHELL_INDEX_CUSTOM or DEFSHELL_INDEX_SH */
#endif /* !DEFSHELL_INDEX */

static Shell    shells[] = {
#ifdef DEFSHELL_CUSTOM
    /*
     * An sh-compatible shell with a non-standard name.
     *
     * Keep this in sync with the "sh" description below, but avoid
     * non-portable features that might not be supplied by all
     * sh-compatible shells.
     */
{
    DEFSHELL_CUSTOM,
    FALSE, "", "", "", 0,
    FALSE, "echo \"%s\"\n", "%s\n", "{ %s \n} || exit $?\n", "'\n'", '#',
    "",
    "",
},
#endif /* DEFSHELL_CUSTOM */
    /*
     * SH description. Echo control is also possible and, under
     * sun UNIX anyway, one can even control error checking.
     */
{
    "sh",
    FALSE, "", "", "", 0,
    FALSE, "echo \"%s\"\n", "%s\n", "{ %s \n} || exit $?\n", "'\n'", '#',
#if defined(MAKE_NATIVE) && defined(__NetBSD__)
    "q",
#else
    "",
#endif
    "",
},
    /*
     * KSH description. 
     */
{
    "ksh",
    TRUE, "set +v", "set -v", "set +v", 6,
    FALSE, "echo \"%s\"\n", "%s\n", "{ %s \n} || exit $?\n", "'\n'", '#',
    "v",
    "",
},
    /*
     * CSH description. The csh can do echo control by playing
     * with the setting of the 'echo' shell variable. Sadly,
     * however, it is unable to do error control nicely.
     */
{
    "csh",
    TRUE, "unset verbose", "set verbose", "unset verbose", 10,
    FALSE, "echo \"%s\"\n", "csh -c \"%s || exit 0\"\n", "", "'\\\n'", '#',
    "v", "e",
},
    /*
     * UNKNOWN.
     */
{
    NULL,
    FALSE, NULL, NULL, NULL, 0,
    FALSE, NULL, NULL, NULL, NULL, 0,
    NULL, NULL,
}
};
static Shell *commandShell = &shells[DEFSHELL_INDEX]; /* this is the shell to
						   * which we pass all
						   * commands in the Makefile.
						   * It is set by the
						   * Job_ParseShell function */
const char *shellPath = NULL,		  	  /* full pathname of
						   * executable image */
           *shellName = NULL;		      	  /* last component of shell */
char *shellErrFlag = NULL;
static const char *shellArgv = NULL;		  /* Custom shell args */


STATIC Job	*job_table;	/* The structures that describe them */
STATIC Job	*job_table_end;	/* job_table + maxJobs */
static int	wantToken;	/* we want a token */
static int lurking_children = 0;
static int make_suspended = 0;	/* non-zero if we've seen a SIGTSTP (etc) */

/*
 * Set of descriptors of pipes connected to
 * the output channels of children
 */
static struct pollfd *fds = NULL;
static Job **jobfds = NULL;
static int nfds = 0;
static void watchfd(Job *);
static void clearfd(Job *);
static int readyfd(Job *);

STATIC GNode   	*lastNode;	/* The node for which output was most recently
				 * produced. */
static char *targPrefix = NULL; /* What we print at the start of TARG_FMT */
static Job tokenWaitJob;	/* token wait pseudo-job */

static Job childExitJob;	/* child exit pseudo-job */
#define	CHILD_EXIT	"."
#define	DO_JOB_RESUME	"R"

#define TARG_FMT  "%s %s ---\n" /* Default format */
#define MESSAGE(fp, gn) \
	if (maxJobs != 1 && targPrefix && *targPrefix) \
	    (void)fprintf(fp, TARG_FMT, targPrefix, gn->name)

static sigset_t caught_signals;	/* Set of signals we handle */

static void JobChildSig(int);
static void JobContinueSig(int);
static Job *JobFindPid(int, int, Boolean);
static int JobPrintCommand(void *, void *);
static int JobSaveCommand(void *, void *);
static void JobClose(Job *);
static void JobExec(Job *, char **);
static void JobMakeArgv(Job *, char **);
static int JobStart(GNode *, int);
static char *JobOutput(Job *, char *, char *, int);
static void JobDoOutput(Job *, Boolean);
static Shell *JobMatchShell(const char *);
static void JobInterrupt(int, int) MAKE_ATTR_DEAD;
static void JobRestartJobs(void);
static void JobTokenAdd(void);
static void JobSigLock(sigset_t *);
static void JobSigUnlock(sigset_t *);
static void JobSigReset(void);

#if !defined(MALLOC_OPTIONS)
# define MALLOC_OPTIONS "A"
#endif
const char *malloc_options= MALLOC_OPTIONS;

static void
job_table_dump(const char *where)
{
    Job *job;

    fprintf(debug_file, "job table @ %s\n", where);
    for (job = job_table; job < job_table_end; job++) {
	fprintf(debug_file, "job %d, status %d, flags %d, pid %d\n",
	    (int)(job - job_table), job->job_state, job->flags, job->pid);
    }
}

/*
 * Delete the target of a failed, interrupted, or otherwise
 * unsuccessful job unless inhibited by .PRECIOUS.
 */
static void
JobDeleteTarget(GNode *gn)
{
	if ((gn->type & (OP_JOIN|OP_PHONY)) == 0 && !Targ_Precious(gn)) {
	    char *file = (gn->path == NULL ? gn->name : gn->path);
	    if (!noExecute && eunlink(file) != -1) {
		Error("*** %s removed", file);
	    }
	}
}

/*
 * JobSigLock/JobSigUnlock
 *
 * Signal lock routines to get exclusive access. Currently used to
 * protect `jobs' and `stoppedJobs' list manipulations.
 */
static void JobSigLock(sigset_t *omaskp)
{
	if (sigprocmask(SIG_BLOCK, &caught_signals, omaskp) != 0) {
		Punt("JobSigLock: sigprocmask: %s", strerror(errno));
		sigemptyset(omaskp);
	}
}

static void JobSigUnlock(sigset_t *omaskp)
{
	(void)sigprocmask(SIG_SETMASK, omaskp, NULL);
}

static void
JobCreatePipe(Job *job, int minfd)
{
    int i, fd, flags;

    if (pipe(job->jobPipe) == -1)
	Punt("Cannot create pipe: %s", strerror(errno));

    for (i = 0; i < 2; i++) {
       /* Avoid using low numbered fds */
       fd = fcntl(job->jobPipe[i], F_DUPFD, minfd);
       if (fd != -1) {
	   close(job->jobPipe[i]);
	   job->jobPipe[i] = fd;
       }
    }
    
    /* Set close-on-exec flag for both */
    if (fcntl(job->jobPipe[0], F_SETFD, FD_CLOEXEC) == -1)
	Punt("Cannot set close-on-exec: %s", strerror(errno));
    if (fcntl(job->jobPipe[1], F_SETFD, FD_CLOEXEC) == -1)
	Punt("Cannot set close-on-exec: %s", strerror(errno));

    /*
     * We mark the input side of the pipe non-blocking; we poll(2) the
     * pipe when we're waiting for a job token, but we might lose the
     * race for the token when a new one becomes available, so the read 
     * from the pipe should not block.
     */
    flags = fcntl(job->jobPipe[0], F_GETFL, 0);
    if (flags == -1)
	Punt("Cannot get flags: %s", strerror(errno));
    flags |= O_NONBLOCK;
    if (fcntl(job->jobPipe[0], F_SETFL, flags) == -1)
	Punt("Cannot set flags: %s", strerror(errno));
}

/*-
 *-----------------------------------------------------------------------
 * JobCondPassSig --
 *	Pass a signal to a job
 *
 * Input:
 *	signop		Signal to send it
 *
 * Side Effects:
 *	None, except the job may bite it.
 *
 *-----------------------------------------------------------------------
 */
static void
JobCondPassSig(int signo)
{
    Job *job;

    if (DEBUG(JOB)) {
	(void)fprintf(debug_file, "JobCondPassSig(%d) called.\n", signo);
    }

    for (job = job_table; job < job_table_end; job++) {
	if (job->job_state != JOB_ST_RUNNING)
	    continue;
	if (DEBUG(JOB)) {
	    (void)fprintf(debug_file,
			   "JobCondPassSig passing signal %d to child %d.\n",
			   signo, job->pid);
	}
	KILLPG(job->pid, signo);
    }
}

/*-
 *-----------------------------------------------------------------------
 * JobChldSig --
 *	SIGCHLD handler.
 *
 * Input:
 *	signo		The signal number we've received
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Sends a token on the child exit pipe to wake us up from
 *	select()/poll().
 *
 *-----------------------------------------------------------------------
 */
static void
JobChildSig(int signo MAKE_ATTR_UNUSED)
{
    while (write(childExitJob.outPipe, CHILD_EXIT, 1) == -1 && errno == EAGAIN)
	continue;
}


/*-
 *-----------------------------------------------------------------------
 * JobContinueSig --
 *	Resume all stopped jobs.
 *
 * Input:
 *	signo		The signal number we've received
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Jobs start running again.
 *
 *-----------------------------------------------------------------------
 */
static void
JobContinueSig(int signo MAKE_ATTR_UNUSED)
{
    /*
     * Defer sending to SIGCONT to our stopped children until we return
     * from the signal handler.
     */
    while (write(childExitJob.outPipe, DO_JOB_RESUME, 1) == -1 &&
	errno == EAGAIN)
	continue;
}

/*-
 *-----------------------------------------------------------------------
 * JobPassSig --
 *	Pass a signal on to all jobs, then resend to ourselves.
 *
 * Input:
 *	signo		The signal number we've received
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	We die by the same signal.
 *
 *-----------------------------------------------------------------------
 */
MAKE_ATTR_DEAD static void
JobPassSig_int(int signo)
{
    /* Run .INTERRUPT target then exit */
    JobInterrupt(TRUE, signo);
}

MAKE_ATTR_DEAD static void
JobPassSig_term(int signo)
{
    /* Dont run .INTERRUPT target then exit */
    JobInterrupt(FALSE, signo);
}

static void
JobPassSig_suspend(int signo)
{
    sigset_t nmask, omask;
    struct sigaction act;

    /* Suppress job started/continued messages */
    make_suspended = 1;

    /* Pass the signal onto every job */
    JobCondPassSig(signo);

    /*
     * Send ourselves the signal now we've given the message to everyone else.
     * Note we block everything else possible while we're getting the signal.
     * This ensures that all our jobs get continued when we wake up before
     * we take any other signal.
     */
    sigfillset(&nmask);
    sigdelset(&nmask, signo);
    (void)sigprocmask(SIG_SETMASK, &nmask, &omask);

    act.sa_handler = SIG_DFL;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    (void)sigaction(signo, &act, NULL);

    if (DEBUG(JOB)) {
	(void)fprintf(debug_file,
		       "JobPassSig passing signal %d to self.\n", signo);
    }

    (void)kill(getpid(), signo);

    /*
     * We've been continued.
     *
     * A whole host of signals continue to happen!
     * SIGCHLD for any processes that actually suspended themselves.
     * SIGCHLD for any processes that exited while we were alseep.
     * The SIGCONT that actually caused us to wakeup.
     *
     * Since we defer passing the SIGCONT on to our children until
     * the main processing loop, we can be sure that all the SIGCHLD
     * events will have happened by then - and that the waitpid() will
     * collect the child 'suspended' events.
     * For correct sequencing we just need to ensure we process the
     * waitpid() before passign on the SIGCONT.
     *
     * In any case nothing else is needed here.
     */

    /* Restore handler and signal mask */
    act.sa_handler = JobPassSig_suspend;
    (void)sigaction(signo, &act, NULL);
    (void)sigprocmask(SIG_SETMASK, &omask, NULL);
}

/*-
 *-----------------------------------------------------------------------
 * JobFindPid  --
 *	Compare the pid of the job with the given pid and return 0 if they
 *	are equal. This function is called from Job_CatchChildren
 *	to find the job descriptor of the finished job.
 *
 * Input:
 *	job		job to examine
 *	pid		process id desired
 *
 * Results:
 *	Job with matching pid
 *
 * Side Effects:
 *	None
 *-----------------------------------------------------------------------
 */
static Job *
JobFindPid(int pid, int status, Boolean isJobs)
{
    Job *job;

    for (job = job_table; job < job_table_end; job++) {
	if ((job->job_state == status) && job->pid == pid)
	    return job;
    }
    if (DEBUG(JOB) && isJobs)
	job_table_dump("no pid");
    return NULL;
}

/*-
 *-----------------------------------------------------------------------
 * JobPrintCommand  --
 *	Put out another command for the given job. If the command starts
 *	with an @ or a - we process it specially. In the former case,
 *	so long as the -s and -n flags weren't given to make, we stick
 *	a shell-specific echoOff command in the script. In the latter,
 *	we ignore errors for the entire job, unless the shell has error
 *	control.
 *	If the command is just "..." we take all future commands for this
 *	job to be commands to be executed once the entire graph has been
 *	made and return non-zero to signal that the end of the commands
 *	was reached. These commands are later attached to the postCommands
 *	node and executed by Job_End when all things are done.
 *	This function is called from JobStart via Lst_ForEach.
 *
 * Input:
 *	cmdp		command string to print
 *	jobp		job for which to print it
 *
 * Results:
 *	Always 0, unless the command was "..."
 *
 * Side Effects:
 *	If the command begins with a '-' and the shell has no error control,
 *	the JOB_IGNERR flag is set in the job descriptor.
 *	If the command is "..." and we're not ignoring such things,
 *	tailCmds is set to the successor node of the cmd.
 *	numCommands is incremented if the command is actually printed.
 *-----------------------------------------------------------------------
 */
static int
JobPrintCommand(void *cmdp, void *jobp)
{
    Boolean	  noSpecials;	    /* true if we shouldn't worry about
				     * inserting special commands into
				     * the input stream. */
    Boolean       shutUp = FALSE;   /* true if we put a no echo command
				     * into the command file */
    Boolean	  errOff = FALSE;   /* true if we turned error checking
				     * off before printing the command
				     * and need to turn it back on */
    const char    *cmdTemplate;	    /* Template to use when printing the
				     * command */
    char    	  *cmdStart;	    /* Start of expanded command */
    char	  *escCmd = NULL;    /* Command with quotes/backticks escaped */
    char     	  *cmd = (char *)cmdp;
    Job           *job = (Job *)jobp;
    int           i, j;

    noSpecials = NoExecute(job->node);

    if (strcmp(cmd, "...") == 0) {
	job->node->type |= OP_SAVE_CMDS;
	if ((job->flags & JOB_IGNDOTS) == 0) {
	    job->tailCmds = Lst_Succ(Lst_Member(job->node->commands,
						cmd));
	    return 1;
	}
	return 0;
    }

#define DBPRINTF(fmt, arg) if (DEBUG(JOB)) {	\
	(void)fprintf(debug_file, fmt, arg); 	\
    }						\
   (void)fprintf(job->cmdFILE, fmt, arg);	\
   (void)fflush(job->cmdFILE);

    numCommands += 1;

    cmdStart = cmd = Var_Subst(NULL, cmd, job->node, VARF_WANTRES);

    cmdTemplate = "%s\n";

    /*
     * Check for leading @' and -'s to control echoing and error checking.
     */
    while (*cmd == '@' || *cmd == '-' || (*cmd == '+')) {
	switch (*cmd) {
	case '@':
	    shutUp = DEBUG(LOUD) ? FALSE : TRUE;
	    break;
	case '-':
	    errOff = TRUE;
	    break;
	case '+':
	    if (noSpecials) {
		/*
		 * We're not actually executing anything...
		 * but this one needs to be - use compat mode just for it.
		 */
		CompatRunCommand(cmdp, job->node);
		free(cmdStart);
		return 0;
	    }
	    break;
	}
	cmd++;
    }

    while (isspace((unsigned char) *cmd))
	cmd++;

    /*
     * If the shell doesn't have error control the alternate echo'ing will
     * be done (to avoid showing additional error checking code) 
     * and this will need the characters '$ ` \ "' escaped
     */

    if (!commandShell->hasErrCtl) {
	/* Worst that could happen is every char needs escaping. */
	escCmd = bmake_malloc((strlen(cmd) * 2) + 1);
	for (i = 0, j= 0; cmd[i] != '\0'; i++, j++) {
		if (cmd[i] == '$' || cmd[i] == '`' || cmd[i] == '\\' || 
			cmd[i] == '"')
			escCmd[j++] = '\\';
		escCmd[j] = cmd[i];	
	}
	escCmd[j] = 0;
    }

    if (shutUp) {
	if (!(job->flags & JOB_SILENT) && !noSpecials &&
	    commandShell->hasEchoCtl) {
		DBPRINTF("%s\n", commandShell->echoOff);
	} else {
	    if (commandShell->hasErrCtl)
		shutUp = FALSE;
	}
    }

    if (errOff) {
	if (!noSpecials) {
	    if (commandShell->hasErrCtl) {
		/*
		 * we don't want the error-control commands showing
		 * up either, so we turn off echoing while executing
		 * them. We could put another field in the shell
		 * structure to tell JobDoOutput to look for this
		 * string too, but why make it any more complex than
		 * it already is?
		 */
		if (!(job->flags & JOB_SILENT) && !shutUp &&
		    commandShell->hasEchoCtl) {
			DBPRINTF("%s\n", commandShell->echoOff);
			DBPRINTF("%s\n", commandShell->ignErr);
			DBPRINTF("%s\n", commandShell->echoOn);
		} else {
			DBPRINTF("%s\n", commandShell->ignErr);
		}
	    } else if (commandShell->ignErr &&
		      (*commandShell->ignErr != '\0'))
	    {
		/*
		 * The shell has no error control, so we need to be
		 * weird to get it to ignore any errors from the command.
		 * If echoing is turned on, we turn it off and use the
		 * errCheck template to echo the command. Leave echoing
		 * off so the user doesn't see the weirdness we go through
		 * to ignore errors. Set cmdTemplate to use the weirdness
		 * instead of the simple "%s\n" template.
		 */
		job->flags |= JOB_IGNERR;
		if (!(job->flags & JOB_SILENT) && !shutUp) {
			if (commandShell->hasEchoCtl) {
				DBPRINTF("%s\n", commandShell->echoOff);
			}
			DBPRINTF(commandShell->errCheck, escCmd);
			shutUp = TRUE;
		} else {
			if (!shutUp) {
				DBPRINTF(commandShell->errCheck, escCmd);
			}
		}
		cmdTemplate = commandShell->ignErr;
		/*
		 * The error ignoration (hee hee) is already taken care
		 * of by the ignErr template, so pretend error checking
		 * is still on.
		 */
		errOff = FALSE;
	    } else {
		errOff = FALSE;
	    }
	} else {
	    errOff = FALSE;
	}
    } else {

	/* 
	 * If errors are being checked and the shell doesn't have error control
	 * but does supply an errOut template, then setup commands to run
	 * through it.
	 */

	if (!commandShell->hasErrCtl && commandShell->errOut && 
	    (*commandShell->errOut != '\0')) {
		if (!(job->flags & JOB_SILENT) && !shutUp) {
			if (commandShell->hasEchoCtl) {
				DBPRINTF("%s\n", commandShell->echoOff);
			}
			DBPRINTF(commandShell->errCheck, escCmd);
			shutUp = TRUE;
		}
		/* If it's a comment line or blank, treat as an ignored error */
		if ((escCmd[0] == commandShell->commentChar) ||
		    (escCmd[0] == 0))
			cmdTemplate = commandShell->ignErr;
		else
			cmdTemplate = commandShell->errOut;
		errOff = FALSE;
	}
    }

    if (DEBUG(SHELL) && strcmp(shellName, "sh") == 0 &&
	(job->flags & JOB_TRACED) == 0) {
	    DBPRINTF("set -%s\n", "x");
	    job->flags |= JOB_TRACED;
    }
    
    DBPRINTF(cmdTemplate, cmd);
    free(cmdStart);
    free(escCmd);
    if (errOff) {
	/*
	 * If echoing is already off, there's no point in issuing the
	 * echoOff command. Otherwise we issue it and pretend it was on
	 * for the whole command...
	 */
	if (!shutUp && !(job->flags & JOB_SILENT) && commandShell->hasEchoCtl){
	    DBPRINTF("%s\n", commandShell->echoOff);
	    shutUp = TRUE;
	}
	DBPRINTF("%s\n", commandShell->errCheck);
    }
    if (shutUp && commandShell->hasEchoCtl) {
	DBPRINTF("%s\n", commandShell->echoOn);
    }
    return 0;
}

/*-
 *-----------------------------------------------------------------------
 * JobSaveCommand --
 *	Save a command to be executed when everything else is done.
 *	Callback function for JobFinish...
 *
 * Results:
 *	Always returns 0
 *
 * Side Effects:
 *	The command is tacked onto the end of postCommands's commands list.
 *
 *-----------------------------------------------------------------------
 */
static int
JobSaveCommand(void *cmd, void *gn)
{
    cmd = Var_Subst(NULL, (char *)cmd, (GNode *)gn, VARF_WANTRES);
    (void)Lst_AtEnd(postCommands->commands, cmd);
    return(0);
}


/*-
 *-----------------------------------------------------------------------
 * JobClose --
 *	Called to close both input and output pipes when a job is finished.
 *
 * Results:
 *	Nada
 *
 * Side Effects:
 *	The file descriptors associated with the job are closed.
 *
 *-----------------------------------------------------------------------
 */
static void
JobClose(Job *job)
{
    clearfd(job);
    (void)close(job->outPipe);
    job->outPipe = -1;

    JobDoOutput(job, TRUE);
    (void)close(job->inPipe);
    job->inPipe = -1;
}

/*-
 *-----------------------------------------------------------------------
 * JobFinish  --
 *	Do final processing for the given job including updating
 *	parents and starting new jobs as available/necessary. Note
 *	that we pay no attention to the JOB_IGNERR flag here.
 *	This is because when we're called because of a noexecute flag
 *	or something, jstat.w_status is 0 and when called from
 *	Job_CatchChildren, the status is zeroed if it s/b ignored.
 *
 * Input:
 *	job		job to finish
 *	status		sub-why job went away
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	Final commands for the job are placed on postCommands.
 *
 *	If we got an error and are aborting (aborting == ABORT_ERROR) and
 *	the job list is now empty, we are done for the day.
 *	If we recognized an error (errors !=0), we set the aborting flag
 *	to ABORT_ERROR so no more jobs will be started.
 *-----------------------------------------------------------------------
 */
/*ARGSUSED*/
static void
JobFinish (Job *job, WAIT_T status)
{
    Boolean 	 done, return_job_token;

    if (DEBUG(JOB)) {
	fprintf(debug_file, "Jobfinish: %d [%s], status %d\n",
				job->pid, job->node->name, status);
    }

    if ((WIFEXITED(status) &&
	 (((WEXITSTATUS(status) != 0) && !(job->flags & JOB_IGNERR)))) ||
	WIFSIGNALED(status))
    {
	/*
	 * If it exited non-zero and either we're doing things our
	 * way or we're not ignoring errors, the job is finished.
	 * Similarly, if the shell died because of a signal
	 * the job is also finished. In these
	 * cases, finish out the job's output before printing the exit
	 * status...
	 */
	JobClose(job);
	if (job->cmdFILE != NULL && job->cmdFILE != stdout) {
	   (void)fclose(job->cmdFILE);
	   job->cmdFILE = NULL;
	}
	done = TRUE;
    } else if (WIFEXITED(status)) {
	/*
	 * Deal with ignored errors in -B mode. We need to print a message
	 * telling of the ignored error as well as setting status.w_status
	 * to 0 so the next command gets run. To do this, we set done to be
	 * TRUE if in -B mode and the job exited non-zero.
	 */
	done = WEXITSTATUS(status) != 0;
	/*
	 * Old comment said: "Note we don't
	 * want to close down any of the streams until we know we're at the
	 * end."
	 * But we do. Otherwise when are we going to print the rest of the
	 * stuff?
	 */
	JobClose(job);
    } else {
	/*
	 * No need to close things down or anything.
	 */
	done = FALSE;
    }

    if (done) {
	if (WIFEXITED(status)) {
	    if (DEBUG(JOB)) {
		(void)fprintf(debug_file, "Process %d [%s] exited.\n",
				job->pid, job->node->name);
	    }
	    if (WEXITSTATUS(status) != 0) {
		if (job->node != lastNode) {
		    MESSAGE(stdout, job->node);
		    lastNode = job->node;
		}
#ifdef USE_META
		if (useMeta) {
		    meta_job_error(job, job->node, job->flags, WEXITSTATUS(status));
		}
#endif
		(void)printf("*** [%s] Error code %d%s\n",
				job->node->name,
			       WEXITSTATUS(status),
			       (job->flags & JOB_IGNERR) ? " (ignored)" : "");
		if (job->flags & JOB_IGNERR) {
		    WAIT_STATUS(status) = 0;
		} else {
		    if (deleteOnError) {
			JobDeleteTarget(job->node);
		    }
		    PrintOnError(job->node, NULL);
		}
	    } else if (DEBUG(JOB)) {
		if (job->node != lastNode) {
		    MESSAGE(stdout, job->node);
		    lastNode = job->node;
		}
		(void)printf("*** [%s] Completed successfully\n",
				job->node->name);
	    }
	} else {
	    if (job->node != lastNode) {
		MESSAGE(stdout, job->node);
		lastNode = job->node;
	    }
	    (void)printf("*** [%s] Signal %d\n",
			job->node->name, WTERMSIG(status));
	    if (deleteOnError) {
		JobDeleteTarget(job->node);
	    }
	}
	(void)fflush(stdout);
    }

#ifdef USE_META
    if (useMeta) {
	int x;

	if ((x = meta_job_finish(job)) != 0 && status == 0) {
	    status = x;
	}
    }
#endif
    
    return_job_token = FALSE;

    Trace_Log(JOBEND, job);
    if (!(job->flags & JOB_SPECIAL)) {
	if ((WAIT_STATUS(status) != 0) ||
		(aborting == ABORT_ERROR) ||
		(aborting == ABORT_INTERRUPT))
	    return_job_token = TRUE;
    }

    if ((aborting != ABORT_ERROR) && (aborting != ABORT_INTERRUPT) &&
	(WAIT_STATUS(status) == 0)) {
	/*
	 * As long as we aren't aborting and the job didn't return a non-zero
	 * status that we shouldn't ignore, we call Make_Update to update
	 * the parents. In addition, any saved commands for the node are placed
	 * on the .END target.
	 */
	if (job->tailCmds != NULL) {
	    Lst_ForEachFrom(job->node->commands, job->tailCmds,
			     JobSaveCommand,
			    job->node);
	}
	job->node->made = MADE;
	if (!(job->flags & JOB_SPECIAL))
	    return_job_token = TRUE;
	Make_Update(job->node);
	job->job_state = JOB_ST_FREE;
    } else if (WAIT_STATUS(status)) {
	errors += 1;
	job->job_state = JOB_ST_FREE;
    }

    /*
     * Set aborting if any error.
     */
    if (errors && !keepgoing && (aborting != ABORT_INTERRUPT)) {
	/*
	 * If we found any errors in this batch of children and the -k flag
	 * wasn't given, we set the aborting flag so no more jobs get
	 * started.
	 */
	aborting = ABORT_ERROR;
    }

    if (return_job_token)
	Job_TokenReturn();

    if (aborting == ABORT_ERROR && jobTokensRunning == 0) {
	/*
	 * If we are aborting and the job table is now empty, we finish.
	 */
	Finish(errors);
    }
}

/*-
 *-----------------------------------------------------------------------
 * Job_Touch --
 *	Touch the given target. Called by JobStart when the -t flag was
 *	given
 *
 * Input:
 *	gn		the node of the file to touch
 *	silent		TRUE if should not print message
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	The data modification of the file is changed. In addition, if the
 *	file did not exist, it is created.
 *-----------------------------------------------------------------------
 */
void
Job_Touch(GNode *gn, Boolean silent)
{
    int		  streamID;   	/* ID of stream opened to do the touch */
    struct utimbuf times;	/* Times for utime() call */

    if (gn->type & (OP_JOIN|OP_USE|OP_USEBEFORE|OP_EXEC|OP_OPTIONAL|
	OP_SPECIAL|OP_PHONY)) {
	/*
	 * .JOIN, .USE, .ZEROTIME and .OPTIONAL targets are "virtual" targets
	 * and, as such, shouldn't really be created.
	 */
	return;
    }

    if (!silent || NoExecute(gn)) {
	(void)fprintf(stdout, "touch %s\n", gn->name);
	(void)fflush(stdout);
    }

    if (NoExecute(gn)) {
	return;
    }

    if (gn->type & OP_ARCHV) {
	Arch_Touch(gn);
    } else if (gn->type & OP_LIB) {
	Arch_TouchLib(gn);
    } else {
	char	*file = gn->path ? gn->path : gn->name;

	times.actime = times.modtime = now;
	if (utime(file, &times) < 0){
	    streamID = open(file, O_RDWR | O_CREAT, 0666);

	    if (streamID >= 0) {
		char	c;

		/*
		 * Read and write a byte to the file to change the
		 * modification time, then close the file.
		 */
		if (read(streamID, &c, 1) == 1) {
		    (void)lseek(streamID, (off_t)0, SEEK_SET);
		    while (write(streamID, &c, 1) == -1 && errno == EAGAIN)
			continue;
		}

		(void)close(streamID);
	    } else {
		(void)fprintf(stdout, "*** couldn't touch %s: %s",
			       file, strerror(errno));
		(void)fflush(stdout);
	    }
	}
    }
}

/*-
 *-----------------------------------------------------------------------
 * Job_CheckCommands --
 *	Make sure the given node has all the commands it needs.
 *
 * Input:
 *	gn		The target whose commands need verifying
 *	abortProc	Function to abort with message
 *
 * Results:
 *	TRUE if the commands list is/was ok.
 *
 * Side Effects:
 *	The node will have commands from the .DEFAULT rule added to it
 *	if it needs them.
 *-----------------------------------------------------------------------
 */
Boolean
Job_CheckCommands(GNode *gn, void (*abortProc)(const char *, ...))
{
    if (OP_NOP(gn->type) && Lst_IsEmpty(gn->commands) &&
	((gn->type & OP_LIB) == 0 || Lst_IsEmpty(gn->children))) {
	/*
	 * No commands. Look for .DEFAULT rule from which we might infer
	 * commands
	 */
	if ((DEFAULT != NULL) && !Lst_IsEmpty(DEFAULT->commands) &&
		(gn->type & OP_SPECIAL) == 0) {
	    char *p1;
	    /*
	     * Make only looks for a .DEFAULT if the node was never the
	     * target of an operator, so that's what we do too. If
	     * a .DEFAULT was given, we substitute its commands for gn's
	     * commands and set the IMPSRC variable to be the target's name
	     * The DEFAULT node acts like a transformation rule, in that
	     * gn also inherits any attributes or sources attached to
	     * .DEFAULT itself.
	     */
	    Make_HandleUse(DEFAULT, gn);
	    Var_Set(IMPSRC, Var_Value(TARGET, gn, &p1), gn, 0);
	    free(p1);
	} else if (Dir_MTime(gn, 0) == 0 && (gn->type & OP_SPECIAL) == 0) {
	    /*
	     * The node wasn't the target of an operator we have no .DEFAULT
	     * rule to go on and the target doesn't already exist. There's
	     * nothing more we can do for this branch. If the -k flag wasn't
	     * given, we stop in our tracks, otherwise we just don't update
	     * this node's parents so they never get examined.
	     */
	    static const char msg[] = ": don't know how to make";

	    if (gn->flags & FROM_DEPEND) {
		if (!Job_RunTarget(".STALE", gn->fname))
		    fprintf(stdout, "%s: %s, %d: ignoring stale %s for %s\n",
			progname, gn->fname, gn->lineno, makeDependfile,
			gn->name);
		return TRUE;
	    }

	    if (gn->type & OP_OPTIONAL) {
		(void)fprintf(stdout, "%s%s %s (ignored)\n", progname,
		    msg, gn->name);
		(void)fflush(stdout);
	    } else if (keepgoing) {
		(void)fprintf(stdout, "%s%s %s (continuing)\n", progname,
		    msg, gn->name);
		(void)fflush(stdout);
  		return FALSE;
	    } else {
		(*abortProc)("%s%s %s. Stop", progname, msg, gn->name);
		return FALSE;
	    }
	}
    }
    return TRUE;
}

/*-
 *-----------------------------------------------------------------------
 * JobExec --
 *	Execute the shell for the given job. Called from JobStart
 *
 * Input:
 *	job		Job to execute
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	A shell is executed, outputs is altered and the Job structure added
 *	to the job table.
 *
 *-----------------------------------------------------------------------
 */
static void
JobExec(Job *job, char **argv)
{
    int	    	  cpid;	    	/* ID of new child */
    sigset_t	  mask;

    job->flags &= ~JOB_TRACED;

    if (DEBUG(JOB)) {
	int 	  i;

	(void)fprintf(debug_file, "Running %s %sly\n", job->node->name, "local");
	(void)fprintf(debug_file, "\tCommand: ");
	for (i = 0; argv[i] != NULL; i++) {
	    (void)fprintf(debug_file, "%s ", argv[i]);
	}
 	(void)fprintf(debug_file, "\n");
    }

    /*
     * Some jobs produce no output and it's disconcerting to have
     * no feedback of their running (since they produce no output, the
     * banner with their name in it never appears). This is an attempt to
     * provide that feedback, even if nothing follows it.
     */
    if ((lastNode != job->node) && !(job->flags & JOB_SILENT)) {
	MESSAGE(stdout, job->node);
	lastNode = job->node;
    }

    /* No interruptions until this job is on the `jobs' list */
    JobSigLock(&mask);

    /* Pre-emptively mark job running, pid still zero though */
    job->job_state = JOB_ST_RUNNING;

    cpid = vFork();
    if (cpid == -1)
	Punt("Cannot vfork: %s", strerror(errno));

    if (cpid == 0) {
	/* Child */
	sigset_t tmask;

#ifdef USE_META
	if (useMeta) {
	    meta_job_child(job);
	}
#endif
	/*
	 * Reset all signal handlers; this is necessary because we also
	 * need to unblock signals before we exec(2).
	 */
	JobSigReset();

	/* Now unblock signals */
	sigemptyset(&tmask);
	JobSigUnlock(&tmask);

	/*
	 * Must duplicate the input stream down to the child's input and
	 * reset it to the beginning (again). Since the stream was marked
	 * close-on-exec, we must clear that bit in the new input.
	 */
	if (dup2(FILENO(job->cmdFILE), 0) == -1) {
	    execError("dup2", "job->cmdFILE");
	    _exit(1);
	}
	if (fcntl(0, F_SETFD, 0) == -1) {
	    execError("fcntl clear close-on-exec", "stdin");
	    _exit(1);
	}
	if (lseek(0, (off_t)0, SEEK_SET) == -1) {
	    execError("lseek to 0", "stdin");
	    _exit(1);
	}

	if (Always_pass_job_queue ||
	    (job->node->type & (OP_MAKE | OP_SUBMAKE))) {
		/*
		 * Pass job token pipe to submakes.
		 */
		if (fcntl(tokenWaitJob.inPipe, F_SETFD, 0) == -1) {
		    execError("clear close-on-exec", "tokenWaitJob.inPipe");
		    _exit(1);
		}
		if (fcntl(tokenWaitJob.outPipe, F_SETFD, 0) == -1) {
		    execError("clear close-on-exec", "tokenWaitJob.outPipe");
		    _exit(1);
		}
	}
	
	/*
	 * Set up the child's output to be routed through the pipe
	 * we've created for it.
	 */
	if (dup2(job->outPipe, 1) == -1) {
	    execError("dup2", "job->outPipe");
	    _exit(1);
	}
	/*
	 * The output channels are marked close on exec. This bit was
	 * duplicated by the dup2(on some systems), so we have to clear
	 * it before routing the shell's error output to the same place as
	 * its standard output.
	 */
	if (fcntl(1, F_SETFD, 0) == -1) {
	    execError("clear close-on-exec", "stdout");
	    _exit(1);
	}
	if (dup2(1, 2) == -1) {
	    execError("dup2", "1, 2");
	    _exit(1);
	}

	/*
	 * We want to switch the child into a different process family so
	 * we can kill it and all its descendants in one fell swoop,
	 * by killing its process family, but not commit suicide.
	 */
#if defined(HAVE_SETPGID)
	(void)setpgid(0, getpid());
#else
#if defined(HAVE_SETSID)
	/* XXX: dsl - I'm sure this should be setpgrp()... */
	(void)setsid();
#else
	(void)setpgrp(0, getpid());
#endif
#endif

	Var_ExportVars();

	(void)execv(shellPath, argv);
	execError("exec", shellPath);
	_exit(1);
    }

    /* Parent, continuing after the child exec */
    job->pid = cpid;

    Trace_Log(JOBSTART, job);

    /*
     * Set the current position in the buffer to the beginning
     * and mark another stream to watch in the outputs mask
     */
    job->curPos = 0;

    watchfd(job);

    if (job->cmdFILE != NULL && job->cmdFILE != stdout) {
	(void)fclose(job->cmdFILE);
	job->cmdFILE = NULL;
    }

    /*
     * Now the job is actually running, add it to the table.
     */
    if (DEBUG(JOB)) {
	fprintf(debug_file, "JobExec(%s): pid %d added to jobs table\n",
		job->node->name, job->pid);
	job_table_dump("job started");
    }
    JobSigUnlock(&mask);
}

/*-
 *-----------------------------------------------------------------------
 * JobMakeArgv --
 *	Create the argv needed to execute the shell for a given job.
 *
 *
 * Results:
 *
 * Side Effects:
 *
 *-----------------------------------------------------------------------
 */
static void
JobMakeArgv(Job *job, char **argv)
{
    int	    	  argc;
    static char args[10]; 	/* For merged arguments */

    argv[0] = UNCONST(shellName);
    argc = 1;

    if ((commandShell->exit && (*commandShell->exit != '-')) ||
	(commandShell->echo && (*commandShell->echo != '-')))
    {
	/*
	 * At least one of the flags doesn't have a minus before it, so
	 * merge them together. Have to do this because the *(&(@*#*&#$#
	 * Bourne shell thinks its second argument is a file to source.
	 * Grrrr. Note the ten-character limitation on the combined arguments.
	 */
	(void)snprintf(args, sizeof(args), "-%s%s",
		      ((job->flags & JOB_IGNERR) ? "" :
		       (commandShell->exit ? commandShell->exit : "")),
		      ((job->flags & JOB_SILENT) ? "" :
		       (commandShell->echo ? commandShell->echo : "")));

	if (args[1]) {
	    argv[argc] = args;
	    argc++;
	}
    } else {
	if (!(job->flags & JOB_IGNERR) && commandShell->exit) {
	    argv[argc] = UNCONST(commandShell->exit);
	    argc++;
	}
	if (!(job->flags & JOB_SILENT) && commandShell->echo) {
	    argv[argc] = UNCONST(commandShell->echo);
	    argc++;
	}
    }
    argv[argc] = NULL;
}

/*-
 *-----------------------------------------------------------------------
 * JobStart  --
 *	Start a target-creation process going for the target described
 *	by the graph node gn.
 *
 * Input:
 *	gn		target to create
 *	flags		flags for the job to override normal ones.
 *			e.g. JOB_SPECIAL or JOB_IGNDOTS
 *	previous	The previous Job structure for this node, if any.
 *
 * Results:
 *	JOB_ERROR if there was an error in the commands, JOB_FINISHED
 *	if there isn't actually anything left to do for the job and
 *	JOB_RUNNING if the job has been started.
 *
 * Side Effects:
 *	A new Job node is created and added to the list of running
 *	jobs. PMake is forked and a child shell created.
 *
 * NB: I'm fairly sure that this code is never called with JOB_SPECIAL set
 *     JOB_IGNDOTS is never set (dsl)
 *     Also the return value is ignored by everyone.
 *-----------------------------------------------------------------------
 */
static int
JobStart(GNode *gn, int flags)
{
    Job		  *job;       /* new job descriptor */
    char	  *argv[10];  /* Argument vector to shell */
    Boolean	  cmdsOK;     /* true if the nodes commands were all right */
    Boolean 	  noExec;     /* Set true if we decide not to run the job */
    int		  tfd;	      /* File descriptor to the temp file */

    for (job = job_table; job < job_table_end; job++) {
	if (job->job_state == JOB_ST_FREE)
	    break;
    }
    if (job >= job_table_end)
	Punt("JobStart no job slots vacant");

    memset(job, 0, sizeof *job);
    job->job_state = JOB_ST_SETUP;
    if (gn->type & OP_SPECIAL)
	flags |= JOB_SPECIAL;

    job->node = gn;
    job->tailCmds = NULL;

    /*
     * Set the initial value of the flags for this job based on the global
     * ones and the node's attributes... Any flags supplied by the caller
     * are also added to the field.
     */
    job->flags = 0;
    if (Targ_Ignore(gn)) {
	job->flags |= JOB_IGNERR;
    }
    if (Targ_Silent(gn)) {
	job->flags |= JOB_SILENT;
    }
    job->flags |= flags;

    /*
     * Check the commands now so any attributes from .DEFAULT have a chance
     * to migrate to the node
     */
    cmdsOK = Job_CheckCommands(gn, Error);

    job->inPollfd = NULL;
    /*
     * If the -n flag wasn't given, we open up OUR (not the child's)
     * temporary file to stuff commands in it. The thing is rd/wr so we don't
     * need to reopen it to feed it to the shell. If the -n flag *was* given,
     * we just set the file to be stdout. Cute, huh?
     */
    if (((gn->type & OP_MAKE) && !(noRecursiveExecute)) ||
	    (!noExecute && !touchFlag)) {
	/*
	 * tfile is the name of a file into which all shell commands are
	 * put. It is removed before the child shell is executed, unless
	 * DEBUG(SCRIPT) is set.
	 */
	char *tfile;
	sigset_t mask;
	/*
	 * We're serious here, but if the commands were bogus, we're
	 * also dead...
	 */
	if (!cmdsOK) {
	    PrintOnError(gn, NULL);	/* provide some clue */
	    DieHorribly();
	}

	JobSigLock(&mask);
	tfd = mkTempFile(TMPPAT, &tfile);
	if (!DEBUG(SCRIPT))
		(void)eunlink(tfile);
	JobSigUnlock(&mask);

	job->cmdFILE = fdopen(tfd, "w+");
	if (job->cmdFILE == NULL) {
	    Punt("Could not fdopen %s", tfile);
	}
	(void)fcntl(FILENO(job->cmdFILE), F_SETFD, FD_CLOEXEC);
	/*
	 * Send the commands to the command file, flush all its buffers then
	 * rewind and remove the thing.
	 */
	noExec = FALSE;

#ifdef USE_META
	if (useMeta) {
	    meta_job_start(job, gn);
	    if (Targ_Silent(gn)) {	/* might have changed */
		job->flags |= JOB_SILENT;
	    }
	}
#endif
	/*
	 * We can do all the commands at once. hooray for sanity
	 */
	numCommands = 0;
	Lst_ForEach(gn->commands, JobPrintCommand, job);

	/*
	 * If we didn't print out any commands to the shell script,
	 * there's not much point in executing the shell, is there?
	 */
	if (numCommands == 0) {
	    noExec = TRUE;
	}

	free(tfile);
    } else if (NoExecute(gn)) {
	/*
	 * Not executing anything -- just print all the commands to stdout
	 * in one fell swoop. This will still set up job->tailCmds correctly.
	 */
	if (lastNode != gn) {
	    MESSAGE(stdout, gn);
	    lastNode = gn;
	}
	job->cmdFILE = stdout;
	/*
	 * Only print the commands if they're ok, but don't die if they're
	 * not -- just let the user know they're bad and keep going. It
	 * doesn't do any harm in this case and may do some good.
	 */
	if (cmdsOK) {
	    Lst_ForEach(gn->commands, JobPrintCommand, job);
	}
	/*
	 * Don't execute the shell, thank you.
	 */
	noExec = TRUE;
    } else {
	/*
	 * Just touch the target and note that no shell should be executed.
	 * Set cmdFILE to stdout to make life easier. Check the commands, too,
	 * but don't die if they're no good -- it does no harm to keep working
	 * up the graph.
	 */
	job->cmdFILE = stdout;
    	Job_Touch(gn, job->flags&JOB_SILENT);
	noExec = TRUE;
    }
    /* Just in case it isn't already... */
    (void)fflush(job->cmdFILE);

    /*
     * If we're not supposed to execute a shell, don't.
     */
    if (noExec) {
	if (!(job->flags & JOB_SPECIAL))
	    Job_TokenReturn();
	/*
	 * Unlink and close the command file if we opened one
	 */
	if (job->cmdFILE != stdout) {
	    if (job->cmdFILE != NULL) {
		(void)fclose(job->cmdFILE);
		job->cmdFILE = NULL;
	    }
	}

	/*
	 * We only want to work our way up the graph if we aren't here because
	 * the commands for the job were no good.
	 */
	if (cmdsOK && aborting == 0) {
	    if (job->tailCmds != NULL) {
		Lst_ForEachFrom(job->node->commands, job->tailCmds,
				JobSaveCommand,
			       job->node);
	    }
	    job->node->made = MADE;
	    Make_Update(job->node);
	}
	job->job_state = JOB_ST_FREE;
	return cmdsOK ? JOB_FINISHED : JOB_ERROR;
    }

    /*
     * Set up the control arguments to the shell. This is based on the flags
     * set earlier for this job.
     */
    JobMakeArgv(job, argv);

    /* Create the pipe by which we'll get the shell's output.  */
    JobCreatePipe(job, 3);

    JobExec(job, argv);
    return(JOB_RUNNING);
}

static char *
JobOutput(Job *job, char *cp, char *endp, int msg)
{
    char *ecp;

    if (commandShell->noPrint) {
	ecp = Str_FindSubstring(cp, commandShell->noPrint);
	while (ecp != NULL) {
	    if (cp != ecp) {
		*ecp = '\0';
		if (!beSilent && msg && job->node != lastNode) {
		    MESSAGE(stdout, job->node);
		    lastNode = job->node;
		}
		/*
		 * The only way there wouldn't be a newline after
		 * this line is if it were the last in the buffer.
		 * however, since the non-printable comes after it,
		 * there must be a newline, so we don't print one.
		 */
		(void)fprintf(stdout, "%s", cp);
		(void)fflush(stdout);
	    }
	    cp = ecp + commandShell->noPLen;
	    if (cp != endp) {
		/*
		 * Still more to print, look again after skipping
		 * the whitespace following the non-printable
		 * command....
		 */
		cp++;
		while (*cp == ' ' || *cp == '\t' || *cp == '\n') {
		    cp++;
		}
		ecp = Str_FindSubstring(cp, commandShell->noPrint);
	    } else {
		return cp;
	    }
	}
    }
    return cp;
}

/*-
 *-----------------------------------------------------------------------
 * JobDoOutput  --
 *	This function is called at different times depending on
 *	whether the user has specified that output is to be collected
 *	via pipes or temporary files. In the former case, we are called
 *	whenever there is something to read on the pipe. We collect more
 *	output from the given job and store it in the job's outBuf. If
 *	this makes up a line, we print it tagged by the job's identifier,
 *	as necessary.
 *	If output has been collected in a temporary file, we open the
 *	file and read it line by line, transfering it to our own
 *	output channel until the file is empty. At which point we
 *	remove the temporary file.
 *	In both cases, however, we keep our figurative eye out for the
 *	'noPrint' line for the shell from which the output came. If
 *	we recognize a line, we don't print it. If the command is not
 *	alone on the line (the character after it is not \0 or \n), we
 *	do print whatever follows it.
 *
 * Input:
 *	job		the job whose output needs printing
 *	finish		TRUE if this is the last time we'll be called
 *			for this job
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	curPos may be shifted as may the contents of outBuf.
 *-----------------------------------------------------------------------
 */
STATIC void
JobDoOutput(Job *job, Boolean finish)
{
    Boolean       gotNL = FALSE;  /* true if got a newline */
    Boolean       fbuf;  	  /* true if our buffer filled up */
    int		  nr;	      	  /* number of bytes read */
    int		  i;	      	  /* auxiliary index into outBuf */
    int		  max;	      	  /* limit for i (end of current data) */
    int		  nRead;      	  /* (Temporary) number of bytes read */

    /*
     * Read as many bytes as will fit in the buffer.
     */
end_loop:
    gotNL = FALSE;
    fbuf = FALSE;

    nRead = read(job->inPipe, &job->outBuf[job->curPos],
		     JOB_BUFSIZE - job->curPos);
    if (nRead < 0) {
	if (errno == EAGAIN)
	    return;
	if (DEBUG(JOB)) {
	    perror("JobDoOutput(piperead)");
	}
	nr = 0;
    } else {
	nr = nRead;
    }

    /*
     * If we hit the end-of-file (the job is dead), we must flush its
     * remaining output, so pretend we read a newline if there's any
     * output remaining in the buffer.
     * Also clear the 'finish' flag so we stop looping.
     */
    if ((nr == 0) && (job->curPos != 0)) {
	job->outBuf[job->curPos] = '\n';
	nr = 1;
	finish = FALSE;
    } else if (nr == 0) {
	finish = FALSE;
    }

    /*
     * Look for the last newline in the bytes we just got. If there is
     * one, break out of the loop with 'i' as its index and gotNL set
     * TRUE.
     */
    max = job->curPos + nr;
    for (i = job->curPos + nr - 1; i >= job->curPos; i--) {
	if (job->outBuf[i] == '\n') {
	    gotNL = TRUE;
	    break;
	} else if (job->outBuf[i] == '\0') {
	    /*
	     * Why?
	     */
	    job->outBuf[i] = ' ';
	}
    }

    if (!gotNL) {
	job->curPos += nr;
	if (job->curPos == JOB_BUFSIZE) {
	    /*
	     * If we've run out of buffer space, we have no choice
	     * but to print the stuff. sigh.
	     */
	    fbuf = TRUE;
	    i = job->curPos;
	}
    }
    if (gotNL || fbuf) {
	/*
	 * Need to send the output to the screen. Null terminate it
	 * first, overwriting the newline character if there was one.
	 * So long as the line isn't one we should filter (according
	 * to the shell description), we print the line, preceded
	 * by a target banner if this target isn't the same as the
	 * one for which we last printed something.
	 * The rest of the data in the buffer are then shifted down
	 * to the start of the buffer and curPos is set accordingly.
	 */
	job->outBuf[i] = '\0';
	if (i >= job->curPos) {
	    char *cp;

	    cp = JobOutput(job, job->outBuf, &job->outBuf[i], FALSE);

	    /*
	     * There's still more in that thar buffer. This time, though,
	     * we know there's no newline at the end, so we add one of
	     * our own free will.
	     */
	    if (*cp != '\0') {
		if (!beSilent && job->node != lastNode) {
		    MESSAGE(stdout, job->node);
		    lastNode = job->node;
		}
#ifdef USE_META
		if (useMeta) {
		    meta_job_output(job, cp, gotNL ? "\n" : "");
		}
#endif
		(void)fprintf(stdout, "%s%s", cp, gotNL ? "\n" : "");
		(void)fflush(stdout);
	    }
	}
	/*
	 * max is the last offset still in the buffer. Move any remaining
	 * characters to the start of the buffer and update the end marker
	 * curPos.
	 */
	if (i < max) {
	    (void)memmove(job->outBuf, &job->outBuf[i + 1], max - (i + 1));
	    job->curPos = max - (i + 1);
	} else {
	    assert(i == max);
	    job->curPos = 0;
	}
    }
    if (finish) {
	/*
	 * If the finish flag is true, we must loop until we hit
	 * end-of-file on the pipe. This is guaranteed to happen
	 * eventually since the other end of the pipe is now closed
	 * (we closed it explicitly and the child has exited). When
	 * we do get an EOF, finish will be set FALSE and we'll fall
	 * through and out.
	 */
	goto end_loop;
    }
}

static void
JobRun(GNode *targ)
{
#ifdef notyet
    /*
     * Unfortunately it is too complicated to run .BEGIN, .END,
     * and .INTERRUPT job in the parallel job module. This has
     * the nice side effect that it avoids a lot of other problems.
     */
    Lst lst = Lst_Init(FALSE);
    Lst_AtEnd(lst, targ);
    (void)Make_Run(lst);
    Lst_Destroy(lst, NULL);
    JobStart(targ, JOB_SPECIAL);
    while (jobTokensRunning) {
	Job_CatchOutput();
    }
#else
    Compat_Make(targ, targ);
    if (targ->made == ERROR) {
	PrintOnError(targ, "\n\nStop.");
	exit(1);
    }
#endif
}

/*-
 *-----------------------------------------------------------------------
 * Job_CatchChildren --
 *	Handle the exit of a child. Called from Make_Make.
 *
 * Input:
 *	block		TRUE if should block on the wait
 *
 * Results:
 *	none.
 *
 * Side Effects:
 *	The job descriptor is removed from the list of children.
 *
 * Notes:
 *	We do waits, blocking or not, according to the wisdom of our
 *	caller, until there are no more children to report. For each
 *	job, call JobFinish to finish things off.
 *
 *-----------------------------------------------------------------------
 */

void
Job_CatchChildren(void)
{
    int    	  pid;	    	/* pid of dead child */
    WAIT_T	  status;   	/* Exit/termination status */

    /*
     * Don't even bother if we know there's no one around.
     */
    if (jobTokensRunning == 0)
	return;

    while ((pid = waitpid((pid_t) -1, &status, WNOHANG | WUNTRACED)) > 0) {
	if (DEBUG(JOB)) {
	    (void)fprintf(debug_file, "Process %d exited/stopped status %x.\n", pid,
	      WAIT_STATUS(status));
	}
	JobReapChild(pid, status, TRUE);
    }
}

/*
 * It is possible that wait[pid]() was called from elsewhere,
 * this lets us reap jobs regardless.
 */
void
JobReapChild(pid_t pid, WAIT_T status, Boolean isJobs)
{
    Job		  *job;	    	/* job descriptor for dead child */

    /*
     * Don't even bother if we know there's no one around.
     */
    if (jobTokensRunning == 0)
	return;

    job = JobFindPid(pid, JOB_ST_RUNNING, isJobs);
    if (job == NULL) {
	if (isJobs) {
	    if (!lurking_children)
		Error("Child (%d) status %x not in table?", pid, status);
	}
	return;				/* not ours */
    }
    if (WIFSTOPPED(status)) {
	if (DEBUG(JOB)) {
	    (void)fprintf(debug_file, "Process %d (%s) stopped.\n",
			  job->pid, job->node->name);
	}
	if (!make_suspended) {
	    switch (WSTOPSIG(status)) {
	    case SIGTSTP:
		(void)printf("*** [%s] Suspended\n", job->node->name);
		break;
	    case SIGSTOP:
		(void)printf("*** [%s] Stopped\n", job->node->name);
		break;
	    default:
		(void)printf("*** [%s] Stopped -- signal %d\n",
			     job->node->name, WSTOPSIG(status));
	    }
	    job->job_suspended = 1;
	}
	(void)fflush(stdout);
	return;
    }

    job->job_state = JOB_ST_FINISHED;
    job->exit_status = WAIT_STATUS(status);

    JobFinish(job, status);
}

/*-
 *-----------------------------------------------------------------------
 * Job_CatchOutput --
 *	Catch the output from our children, if we're using
 *	pipes do so. Otherwise just block time until we get a
 *	signal(most likely a SIGCHLD) since there's no point in
 *	just spinning when there's nothing to do and the reaping
 *	of a child can wait for a while.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	Output is read from pipes if we're piping.
 * -----------------------------------------------------------------------
 */
void
Job_CatchOutput(void)
{
    int nready;
    Job *job;
    int i;

    (void)fflush(stdout);

    /* The first fd in the list is the job token pipe */
    do {
	nready = poll(fds + 1 - wantToken, nfds - 1 + wantToken, POLL_MSEC);
    } while (nready < 0 && errno == EINTR);

    if (nready < 0)
	Punt("poll: %s", strerror(errno));

    if (nready > 0 && readyfd(&childExitJob)) {
	char token = 0;
	ssize_t count;
	count = read(childExitJob.inPipe, &token, 1);
	switch (count) {
	case 0:
	    Punt("unexpected eof on token pipe");
	case -1:
	    Punt("token pipe read: %s", strerror(errno));
	case 1:
	    if (token == DO_JOB_RESUME[0])
		/* Complete relay requested from our SIGCONT handler */
		JobRestartJobs();
	    break;
	default:
	    abort();
	}
	--nready;
    }

    Job_CatchChildren();
    if (nready == 0)
	    return;

    for (i = 2; i < nfds; i++) {
	if (!fds[i].revents)
	    continue;
	job = jobfds[i];
	if (job->job_state == JOB_ST_RUNNING)
	    JobDoOutput(job, FALSE);
	if (--nready == 0)
		return;
    }
}

/*-
 *-----------------------------------------------------------------------
 * Job_Make --
 *	Start the creation of a target. Basically a front-end for
 *	JobStart used by the Make module.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Another job is started.
 *
 *-----------------------------------------------------------------------
 */
void
Job_Make(GNode *gn)
{
    (void)JobStart(gn, 0);
}

void
Shell_Init(void)
{
    if (shellPath == NULL) {
	/*
	 * We are using the default shell, which may be an absolute
	 * path if DEFSHELL_CUSTOM is defined.
	 */
	shellName = commandShell->name;
#ifdef DEFSHELL_CUSTOM
	if (*shellName == '/') {
	    shellPath = shellName;
	    shellName = strrchr(shellPath, '/');
	    shellName++;
	} else
#endif
	shellPath = str_concat(_PATH_DEFSHELLDIR, shellName, STR_ADDSLASH);
    }
    if (commandShell->exit == NULL) {
	commandShell->exit = "";
    }
    if (commandShell->echo == NULL) {
	commandShell->echo = "";
    }
    if (commandShell->hasErrCtl && *commandShell->exit) {
	if (shellErrFlag &&
	    strcmp(commandShell->exit, &shellErrFlag[1]) != 0) {
	    free(shellErrFlag);
	    shellErrFlag = NULL;
	}
	if (!shellErrFlag) {
	    int n = strlen(commandShell->exit) + 2;

	    shellErrFlag = bmake_malloc(n);
	    if (shellErrFlag) {
		snprintf(shellErrFlag, n, "-%s", commandShell->exit);
	    }
	}
    } else if (shellErrFlag) {
	free(shellErrFlag);
	shellErrFlag = NULL;
    }
}

/*-
 * Returns the string literal that is used in the current command shell
 * to produce a newline character.
 */
const char *
Shell_GetNewline(void)
{

    return commandShell->newline;
}

void
Job_SetPrefix(void)
{
    
    if (targPrefix) {
	free(targPrefix);
    } else if (!Var_Exists(MAKE_JOB_PREFIX, VAR_GLOBAL)) {
	Var_Set(MAKE_JOB_PREFIX, "---", VAR_GLOBAL, 0);
    }

    targPrefix = Var_Subst(NULL, "${" MAKE_JOB_PREFIX "}",
			   VAR_GLOBAL, VARF_WANTRES);
}

/*-
 *-----------------------------------------------------------------------
 * Job_Init --
 *	Initialize the process module
 *
 * Input:
 *
 * Results:
 *	none
 *
 * Side Effects:
 *	lists and counters are initialized
 *-----------------------------------------------------------------------
 */
void
Job_Init(void)
{
    Job_SetPrefix();
    /* Allocate space for all the job info */
    job_table = bmake_malloc(maxJobs * sizeof *job_table);
    memset(job_table, 0, maxJobs * sizeof *job_table);
    job_table_end = job_table + maxJobs;
    wantToken =	0;

    aborting = 	  0;
    errors = 	  0;

    lastNode =	  NULL;

    Always_pass_job_queue = getBoolean(MAKE_ALWAYS_PASS_JOB_QUEUE,
				       Always_pass_job_queue);

    Job_error_token = getBoolean(MAKE_JOB_ERROR_TOKEN, Job_error_token);


    /*
     * There is a non-zero chance that we already have children.
     * eg after 'make -f- <<EOF'
     * Since their termination causes a 'Child (pid) not in table' message,
     * Collect the status of any that are already dead, and suppress the
     * error message if there are any undead ones.
     */
    for (;;) {
	int rval, status;
	rval = waitpid((pid_t) -1, &status, WNOHANG);
	if (rval > 0)
	    continue;
	if (rval == 0)
	    lurking_children = 1;
	break;
    }

    Shell_Init();

    JobCreatePipe(&childExitJob, 3);

    /* We can only need to wait for tokens, children and output from each job */
    fds = bmake_malloc(sizeof (*fds) * (2 + maxJobs));
    jobfds = bmake_malloc(sizeof (*jobfds) * (2 + maxJobs));

    /* These are permanent entries and take slots 0 and 1 */
    watchfd(&tokenWaitJob);
    watchfd(&childExitJob);

    sigemptyset(&caught_signals);
    /*
     * Install a SIGCHLD handler.
     */
    (void)bmake_signal(SIGCHLD, JobChildSig);
    sigaddset(&caught_signals, SIGCHLD);

#define ADDSIG(s,h)				\
    if (bmake_signal(s, SIG_IGN) != SIG_IGN) {	\
	sigaddset(&caught_signals, s);		\
	(void)bmake_signal(s, h);			\
    }

    /*
     * Catch the four signals that POSIX specifies if they aren't ignored.
     * JobPassSig will take care of calling JobInterrupt if appropriate.
     */
    ADDSIG(SIGINT, JobPassSig_int)
    ADDSIG(SIGHUP, JobPassSig_term)
    ADDSIG(SIGTERM, JobPassSig_term)
    ADDSIG(SIGQUIT, JobPassSig_term)

    /*
     * There are additional signals that need to be caught and passed if
     * either the export system wants to be told directly of signals or if
     * we're giving each job its own process group (since then it won't get
     * signals from the terminal driver as we own the terminal)
     */
    ADDSIG(SIGTSTP, JobPassSig_suspend)
    ADDSIG(SIGTTOU, JobPassSig_suspend)
    ADDSIG(SIGTTIN, JobPassSig_suspend)
    ADDSIG(SIGWINCH, JobCondPassSig)
    ADDSIG(SIGCONT, JobContinueSig)
#undef ADDSIG

    (void)Job_RunTarget(".BEGIN", NULL);
    postCommands = Targ_FindNode(".END", TARG_CREATE);
}

static void JobSigReset(void)
{
#define DELSIG(s)					\
    if (sigismember(&caught_signals, s)) {		\
	(void)bmake_signal(s, SIG_DFL);			\
    }

    DELSIG(SIGINT)
    DELSIG(SIGHUP)
    DELSIG(SIGQUIT)
    DELSIG(SIGTERM)
    DELSIG(SIGTSTP)
    DELSIG(SIGTTOU)
    DELSIG(SIGTTIN)
    DELSIG(SIGWINCH)
    DELSIG(SIGCONT)
#undef DELSIG
    (void)bmake_signal(SIGCHLD, SIG_DFL);
}

/*-
 *-----------------------------------------------------------------------
 * JobMatchShell --
 *	Find a shell in 'shells' given its name.
 *
 * Results:
 *	A pointer to the Shell structure.
 *
 * Side Effects:
 *	None.
 *
 *-----------------------------------------------------------------------
 */
static Shell *
JobMatchShell(const char *name)
{
    Shell	*sh;

    for (sh = shells; sh->name != NULL; sh++) {
	if (strcmp(name, sh->name) == 0)
		return (sh);
    }
    return NULL;
}

/*-
 *-----------------------------------------------------------------------
 * Job_ParseShell --
 *	Parse a shell specification and set up commandShell, shellPath
 *	and shellName appropriately.
 *
 * Input:
 *	line		The shell spec
 *
 * Results:
 *	FAILURE if the specification was incorrect.
 *
 * Side Effects:
 *	commandShell points to a Shell structure (either predefined or
 *	created from the shell spec), shellPath is the full path of the
 *	shell described by commandShell, while shellName is just the
 *	final component of shellPath.
 *
 * Notes:
 *	A shell specification consists of a .SHELL target, with dependency
 *	operator, followed by a series of blank-separated words. Double
 *	quotes can be used to use blanks in words. A backslash escapes
 *	anything (most notably a double-quote and a space) and
 *	provides the functionality it does in C. Each word consists of
 *	keyword and value separated by an equal sign. There should be no
 *	unnecessary spaces in the word. The keywords are as follows:
 *	    name  	    Name of shell.
 *	    path  	    Location of shell.
 *	    quiet 	    Command to turn off echoing.
 *	    echo  	    Command to turn echoing on
 *	    filter	    Result of turning off echoing that shouldn't be
 *	    	  	    printed.
 *	    echoFlag	    Flag to turn echoing on at the start
 *	    errFlag	    Flag to turn error checking on at the start
 *	    hasErrCtl	    True if shell has error checking control
 *	    newline	    String literal to represent a newline char
 *	    check 	    Command to turn on error checking if hasErrCtl
 *	    	  	    is TRUE or template of command to echo a command
 *	    	  	    for which error checking is off if hasErrCtl is
 *	    	  	    FALSE.
 *	    ignore	    Command to turn off error checking if hasErrCtl
 *	    	  	    is TRUE or template of command to execute a
 *	    	  	    command so as to ignore any errors it returns if
 *	    	  	    hasErrCtl is FALSE.
 *
 *-----------------------------------------------------------------------
 */
ReturnStatus
Job_ParseShell(char *line)
{
    char	**words;
    char	**argv;
    int		argc;
    char	*path;
    Shell	newShell;
    Boolean	fullSpec = FALSE;
    Shell	*sh;

    while (isspace((unsigned char)*line)) {
	line++;
    }

    free(UNCONST(shellArgv));

    memset(&newShell, 0, sizeof(newShell));

    /*
     * Parse the specification by keyword
     */
    words = brk_string(line, &argc, TRUE, &path);
    if (words == NULL) {
	Error("Unterminated quoted string [%s]", line);
	return FAILURE;
    }
    shellArgv = path;

    for (path = NULL, argv = words; argc != 0; argc--, argv++) {
	    if (strncmp(*argv, "path=", 5) == 0) {
		path = &argv[0][5];
	    } else if (strncmp(*argv, "name=", 5) == 0) {
		newShell.name = &argv[0][5];
	    } else {
		if (strncmp(*argv, "quiet=", 6) == 0) {
		    newShell.echoOff = &argv[0][6];
		} else if (strncmp(*argv, "echo=", 5) == 0) {
		    newShell.echoOn = &argv[0][5];
		} else if (strncmp(*argv, "filter=", 7) == 0) {
		    newShell.noPrint = &argv[0][7];
		    newShell.noPLen = strlen(newShell.noPrint);
		} else if (strncmp(*argv, "echoFlag=", 9) == 0) {
		    newShell.echo = &argv[0][9];
		} else if (strncmp(*argv, "errFlag=", 8) == 0) {
		    newShell.exit = &argv[0][8];
		} else if (strncmp(*argv, "hasErrCtl=", 10) == 0) {
		    char c = argv[0][10];
		    newShell.hasErrCtl = !((c != 'Y') && (c != 'y') &&
					   (c != 'T') && (c != 't'));
		} else if (strncmp(*argv, "newline=", 8) == 0) {
		    newShell.newline = &argv[0][8];
		} else if (strncmp(*argv, "check=", 6) == 0) {
		    newShell.errCheck = &argv[0][6];
		} else if (strncmp(*argv, "ignore=", 7) == 0) {
		    newShell.ignErr = &argv[0][7];
		} else if (strncmp(*argv, "errout=", 7) == 0) {
		    newShell.errOut = &argv[0][7];
		} else if (strncmp(*argv, "comment=", 8) == 0) {
		    newShell.commentChar = argv[0][8];
		} else {
		    Parse_Error(PARSE_FATAL, "Unknown keyword \"%s\"",
				*argv);
		    free(words);
		    return(FAILURE);
		}
		fullSpec = TRUE;
	    }
    }

    if (path == NULL) {
	/*
	 * If no path was given, the user wants one of the pre-defined shells,
	 * yes? So we find the one s/he wants with the help of JobMatchShell
	 * and set things up the right way. shellPath will be set up by
	 * Shell_Init.
	 */
	if (newShell.name == NULL) {
	    Parse_Error(PARSE_FATAL, "Neither path nor name specified");
	    free(words);
	    return(FAILURE);
	} else {
	    if ((sh = JobMatchShell(newShell.name)) == NULL) {
		    Parse_Error(PARSE_WARNING, "%s: No matching shell",
				newShell.name);
		    free(words);
		    return(FAILURE);
	    }
	    commandShell = sh;
	    shellName = newShell.name;
	    if (shellPath) {
		/* Shell_Init has already been called!  Do it again. */
		free(UNCONST(shellPath));
		shellPath = NULL;
		Shell_Init();
	    }
	}
    } else {
	/*
	 * The user provided a path. If s/he gave nothing else (fullSpec is
	 * FALSE), try and find a matching shell in the ones we know of.
	 * Else we just take the specification at its word and copy it
	 * to a new location. In either case, we need to record the
	 * path the user gave for the shell.
	 */
	shellPath = path;
	path = strrchr(path, '/');
	if (path == NULL) {
	    path = UNCONST(shellPath);
	} else {
	    path += 1;
	}
	if (newShell.name != NULL) {
	    shellName = newShell.name;
	} else {
	    shellName = path;
	}
	if (!fullSpec) {
	    if ((sh = JobMatchShell(shellName)) == NULL) {
		    Parse_Error(PARSE_WARNING, "%s: No matching shell",
				shellName);
		    free(words);
		    return(FAILURE);
	    }
	    commandShell = sh;
	} else {
	    commandShell = bmake_malloc(sizeof(Shell));
	    *commandShell = newShell;
	}
	/* this will take care of shellErrFlag */
	Shell_Init();
    }

    if (commandShell->echoOn && commandShell->echoOff) {
	commandShell->hasEchoCtl = TRUE;
    }

    if (!commandShell->hasErrCtl) {
	if (commandShell->errCheck == NULL) {
	    commandShell->errCheck = "";
	}
	if (commandShell->ignErr == NULL) {
	    commandShell->ignErr = "%s\n";
	}
    }

    /*
     * Do not free up the words themselves, since they might be in use by the
     * shell specification.
     */
    free(words);
    return SUCCESS;
}

/*-
 *-----------------------------------------------------------------------
 * JobInterrupt --
 *	Handle the receipt of an interrupt.
 *
 * Input:
 *	runINTERRUPT	Non-zero if commands for the .INTERRUPT target
 *			should be executed
 *	signo		signal received
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	All children are killed. Another job will be started if the
 *	.INTERRUPT target was given.
 *-----------------------------------------------------------------------
 */
static void
JobInterrupt(int runINTERRUPT, int signo)
{
    Job		*job;		/* job descriptor in that element */
    GNode	*interrupt;	/* the node describing the .INTERRUPT target */
    sigset_t	mask;
    GNode	*gn;

    aborting = ABORT_INTERRUPT;

    JobSigLock(&mask);

    for (job = job_table; job < job_table_end; job++) {
	if (job->job_state != JOB_ST_RUNNING)
	    continue;

	gn = job->node;

	JobDeleteTarget(gn);
	if (job->pid) {
	    if (DEBUG(JOB)) {
		(void)fprintf(debug_file,
			   "JobInterrupt passing signal %d to child %d.\n",
			   signo, job->pid);
	    }
	    KILLPG(job->pid, signo);
	}
    }

    JobSigUnlock(&mask);

    if (runINTERRUPT && !touchFlag) {
	interrupt = Targ_FindNode(".INTERRUPT", TARG_NOCREATE);
	if (interrupt != NULL) {
	    ignoreErrors = FALSE;
	    JobRun(interrupt);
	}
    }
    Trace_Log(MAKEINTR, 0);
    exit(signo);
}

/*
 *-----------------------------------------------------------------------
 * Job_Finish --
 *	Do final processing such as the running of the commands
 *	attached to the .END target.
 *
 * Results:
 *	Number of errors reported.
 *
 * Side Effects:
 *	None.
 *-----------------------------------------------------------------------
 */
int
Job_Finish(void)
{
    if (postCommands != NULL &&
	(!Lst_IsEmpty(postCommands->commands) ||
	 !Lst_IsEmpty(postCommands->children))) {
	if (errors) {
	    Error("Errors reported so .END ignored");
	} else {
	    JobRun(postCommands);
	}
    }
    return(errors);
}

/*-
 *-----------------------------------------------------------------------
 * Job_End --
 *	Cleanup any memory used by the jobs module
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Memory is freed
 *-----------------------------------------------------------------------
 */
void
Job_End(void)
{
#ifdef CLEANUP
    free(shellArgv);
#endif
}

/*-
 *-----------------------------------------------------------------------
 * Job_Wait --
 *	Waits for all running jobs to finish and returns. Sets 'aborting'
 *	to ABORT_WAIT to prevent other jobs from starting.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Currently running jobs finish.
 *
 *-----------------------------------------------------------------------
 */
void
Job_Wait(void)
{
    aborting = ABORT_WAIT;
    while (jobTokensRunning != 0) {
	Job_CatchOutput();
    }
    aborting = 0;
}

/*-
 *-----------------------------------------------------------------------
 * Job_AbortAll --
 *	Abort all currently running jobs without handling output or anything.
 *	This function is to be called only in the event of a major
 *	error. Most definitely NOT to be called from JobInterrupt.
 *
 * Results:
 *	None
 *
 * Side Effects:
 *	All children are killed, not just the firstborn
 *-----------------------------------------------------------------------
 */
void
Job_AbortAll(void)
{
    Job		*job;	/* the job descriptor in that element */
    WAIT_T	foo;

    aborting = ABORT_ERROR;

    if (jobTokensRunning) {
	for (job = job_table; job < job_table_end; job++) {
	    if (job->job_state != JOB_ST_RUNNING)
		continue;
	    /*
	     * kill the child process with increasingly drastic signals to make
	     * darn sure it's dead.
	     */
	    KILLPG(job->pid, SIGINT);
	    KILLPG(job->pid, SIGKILL);
	}
    }

    /*
     * Catch as many children as want to report in at first, then give up
     */
    while (waitpid((pid_t) -1, &foo, WNOHANG) > 0)
	continue;
}


/*-
 *-----------------------------------------------------------------------
 * JobRestartJobs --
 *	Tries to restart stopped jobs if there are slots available.
 *	Called in process context in response to a SIGCONT.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Resumes jobs.
 *
 *-----------------------------------------------------------------------
 */
static void
JobRestartJobs(void)
{
    Job *job;

    for (job = job_table; job < job_table_end; job++) {
	if (job->job_state == JOB_ST_RUNNING &&
		(make_suspended || job->job_suspended)) {
	    if (DEBUG(JOB)) {
		(void)fprintf(debug_file, "Restarting stopped job pid %d.\n",
			job->pid);
	    }
	    if (job->job_suspended) {
		    (void)printf("*** [%s] Continued\n", job->node->name);
		    (void)fflush(stdout);
	    }
	    job->job_suspended = 0;
	    if (KILLPG(job->pid, SIGCONT) != 0 && DEBUG(JOB)) {
		fprintf(debug_file, "Failed to send SIGCONT to %d\n", job->pid);
	    }
	}
	if (job->job_state == JOB_ST_FINISHED)
	    /* Job exit deferred after calling waitpid() in a signal handler */
	    JobFinish(job, job->exit_status);
    }
    make_suspended = 0;
}

static void
watchfd(Job *job)
{
    if (job->inPollfd != NULL)
	Punt("Watching watched job");

    fds[nfds].fd = job->inPipe;
    fds[nfds].events = POLLIN;
    jobfds[nfds] = job;
    job->inPollfd = &fds[nfds];
    nfds++;
}

static void
clearfd(Job *job)
{
    int i;
    if (job->inPollfd == NULL)
	Punt("Unwatching unwatched job");
    i = job->inPollfd - fds;
    nfds--;
    /*
     * Move last job in table into hole made by dead job.
     */
    if (nfds != i) {
	fds[i] = fds[nfds];
	jobfds[i] = jobfds[nfds];
	jobfds[i]->inPollfd = &fds[i];
    }
    job->inPollfd = NULL;
}

static int
readyfd(Job *job)
{
    if (job->inPollfd == NULL)
	Punt("Polling unwatched job");
    return (job->inPollfd->revents & POLLIN) != 0;
}

/*-
 *-----------------------------------------------------------------------
 * JobTokenAdd --
 *	Put a token into the job pipe so that some make process can start
 *	another job.
 *
 * Side Effects:
 *	Allows more build jobs to be spawned somewhere.
 *
 *-----------------------------------------------------------------------
 */

static void
JobTokenAdd(void)
{
    char tok = JOB_TOKENS[aborting], tok1;

    if (!Job_error_token && aborting == ABORT_ERROR) {
	if (jobTokensRunning == 0)
	    return;
	tok = '+';			/* no error token */
    }

    /* If we are depositing an error token flush everything else */
    while (tok != '+' && read(tokenWaitJob.inPipe, &tok1, 1) == 1)
	continue;

    if (DEBUG(JOB))
	fprintf(debug_file, "(%d) aborting %d, deposit token %c\n",
	    getpid(), aborting, tok);
    while (write(tokenWaitJob.outPipe, &tok, 1) == -1 && errno == EAGAIN)
	continue;
}

/*-
 *-----------------------------------------------------------------------
 * Job_ServerStartTokenAdd --
 *	Prep the job token pipe in the root make process.
 *
 *-----------------------------------------------------------------------
 */

void
Job_ServerStart(int max_tokens, int jp_0, int jp_1)
{
    int i;
    char jobarg[64];
    
    if (jp_0 >= 0 && jp_1 >= 0) {
	/* Pipe passed in from parent */
	tokenWaitJob.inPipe = jp_0;
	tokenWaitJob.outPipe = jp_1;
	(void)fcntl(jp_0, F_SETFD, FD_CLOEXEC);
	(void)fcntl(jp_1, F_SETFD, FD_CLOEXEC);
	return;
    }

    JobCreatePipe(&tokenWaitJob, 15);

    snprintf(jobarg, sizeof(jobarg), "%d,%d",
	    tokenWaitJob.inPipe, tokenWaitJob.outPipe);

    Var_Append(MAKEFLAGS, "-J", VAR_GLOBAL);
    Var_Append(MAKEFLAGS, jobarg, VAR_GLOBAL);			

    /*
     * Preload the job pipe with one token per job, save the one
     * "extra" token for the primary job.
     * 
     * XXX should clip maxJobs against PIPE_BUF -- if max_tokens is
     * larger than the write buffer size of the pipe, we will
     * deadlock here.
     */
    for (i = 1; i < max_tokens; i++)
	JobTokenAdd();
}

/*-
 *-----------------------------------------------------------------------
 * Job_TokenReturn --
 *	Return a withdrawn token to the pool.
 *
 *-----------------------------------------------------------------------
 */

void
Job_TokenReturn(void)
{
    jobTokensRunning--;
    if (jobTokensRunning < 0)
	Punt("token botch");
    if (jobTokensRunning || JOB_TOKENS[aborting] != '+')
	JobTokenAdd();
}

/*-
 *-----------------------------------------------------------------------
 * Job_TokenWithdraw --
 *	Attempt to withdraw a token from the pool.
 *
 * Results:
 *	Returns TRUE if a token was withdrawn, and FALSE if the pool
 *	is currently empty.
 *
 * Side Effects:
 * 	If pool is empty, set wantToken so that we wake up
 *	when a token is released.
 *
 *-----------------------------------------------------------------------
 */


Boolean
Job_TokenWithdraw(void)
{
    char tok, tok1;
    int count;

    wantToken = 0;
    if (DEBUG(JOB))
	fprintf(debug_file, "Job_TokenWithdraw(%d): aborting %d, running %d\n",
		getpid(), aborting, jobTokensRunning);

    if (aborting || (jobTokensRunning >= maxJobs))
	return FALSE;

    count = read(tokenWaitJob.inPipe, &tok, 1);
    if (count == 0)
	Fatal("eof on job pipe!");
    if (count < 0 && jobTokensRunning != 0) {
	if (errno != EAGAIN) {
	    Fatal("job pipe read: %s", strerror(errno));
	}
	if (DEBUG(JOB))
	    fprintf(debug_file, "(%d) blocked for token\n", getpid());
	return FALSE;
    }

    if (count == 1 && tok != '+') {
	/* make being abvorted - remove any other job tokens */
	if (DEBUG(JOB))
	    fprintf(debug_file, "(%d) aborted by token %c\n", getpid(), tok);
	while (read(tokenWaitJob.inPipe, &tok1, 1) == 1)
	    continue;
	/* And put the stopper back */
	while (write(tokenWaitJob.outPipe, &tok, 1) == -1 && errno == EAGAIN)
	    continue;
	Fatal("A failure has been detected in another branch of the parallel make");
    }

    if (count == 1 && jobTokensRunning == 0)
	/* We didn't want the token really */
	while (write(tokenWaitJob.outPipe, &tok, 1) == -1 && errno == EAGAIN)
	    continue;

    jobTokensRunning++;
    if (DEBUG(JOB))
	fprintf(debug_file, "(%d) withdrew token\n", getpid());
    return TRUE;
}

/*-
 *-----------------------------------------------------------------------
 * Job_RunTarget --
 *	Run the named target if found. If a filename is specified, then
 *	set that to the sources.
 *
 * Results:
 *	None
 *
 * Side Effects:
 * 	exits if the target fails.
 *
 *-----------------------------------------------------------------------
 */
Boolean
Job_RunTarget(const char *target, const char *fname) {
    GNode *gn = Targ_FindNode(target, TARG_NOCREATE);

    if (gn == NULL)
	return FALSE;

    if (fname)
	Var_Set(ALLSRC, fname, gn, 0);

    JobRun(gn);
    if (gn->made == ERROR) {
	PrintOnError(gn, "\n\nStop.");
	exit(1);
    }
    return TRUE;
}

#ifdef USE_SELECT
int
emul_poll(struct pollfd *fd, int nfd, int timeout)
{
    fd_set rfds, wfds;
    int i, maxfd, nselect, npoll;
    struct timeval tv, *tvp;
    long usecs;

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);

    maxfd = -1;
    for (i = 0; i < nfd; i++) {
	fd[i].revents = 0;

	if (fd[i].events & POLLIN)
	    FD_SET(fd[i].fd, &rfds);

	if (fd[i].events & POLLOUT)
	    FD_SET(fd[i].fd, &wfds);

	if (fd[i].fd > maxfd)
	    maxfd = fd[i].fd;
    }
    
    if (maxfd >= FD_SETSIZE) {
	Punt("Ran out of fd_set slots; " 
	     "recompile with a larger FD_SETSIZE.");
    }

    if (timeout < 0) {
	tvp = NULL;
    } else {
	usecs = timeout * 1000;
	tv.tv_sec = usecs / 1000000;
	tv.tv_usec = usecs % 1000000;
        tvp = &tv;
    }

    nselect = select(maxfd + 1, &rfds, &wfds, 0, tvp);

    if (nselect <= 0)
	return nselect;

    npoll = 0;
    for (i = 0; i < nfd; i++) {
	if (FD_ISSET(fd[i].fd, &rfds))
	    fd[i].revents |= POLLIN;

	if (FD_ISSET(fd[i].fd, &wfds))
	    fd[i].revents |= POLLOUT;

	if (fd[i].revents)
	    npoll++;
    }

    return npoll;
}
#endif /* USE_SELECT */
