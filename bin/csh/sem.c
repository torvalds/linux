/*	$OpenBSD: sem.c,v 1.25 2024/08/20 23:40:39 guenther Exp $	*/
/*	$NetBSD: sem.c,v 1.9 1995/09/27 00:38:50 jtc Exp $	*/

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
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <stdarg.h>

#include "csh.h"
#include "proc.h"
#include "extern.h"

static void	 vffree(int);
static Char	*splicepipe(struct command *t, Char *);
static void	 doio(struct command *t, int *, int *);
static void	 chkclob(char *);

void
execute(struct command *t, int wanttty, int *pipein, int *pipeout)
{
    bool    forked = 0;
    struct biltins *bifunc;
    int     pid = 0;
    int     pv[2];
    sigset_t sigset;

    static sigset_t csigset;

    static sigset_t ocsigset;
    static int onosigchld = 0;
    static int nosigchld = 0;

    UNREGISTER(forked);
    UNREGISTER(bifunc);
    UNREGISTER(wanttty);

    if (t == 0)
	return;

    if (t->t_dflg & F_AMPERSAND)
	wanttty = 0;
    switch (t->t_dtyp) {

    case NODE_COMMAND:
	if ((t->t_dcom[0][0] & (QUOTE | TRIM)) == QUOTE)
	    (void) memmove(t->t_dcom[0], t->t_dcom[0] + 1,
		(Strlen(t->t_dcom[0] + 1) + 1) * sizeof(Char));
	if ((t->t_dflg & F_REPEAT) == 0)
	    Dfix(t);		/* $ " ' \ */
	if (t->t_dcom[0] == 0)
	    return;
	/* fall into... */

    case NODE_PAREN:
	if (t->t_dflg & F_PIPEOUT)
	    mypipe(pipeout);
	/*
	 * Must do << early so parent will know where input pointer should be.
	 * If noexec then this is all we do.
	 */
	if (t->t_dflg & F_READ) {
	    (void) close(0);
	    heredoc(t->t_dlef);
	    if (noexec)
		(void) close(0);
	}

	set(STRstatus, Strsave(STR0));

	/*
	 * This mess is the necessary kludge to handle the prefix builtins:
	 * nice, nohup, time.  These commands can also be used by themselves,
	 * and this is not handled here. This will also work when loops are
	 * parsed.
	 */
	while (t->t_dtyp == NODE_COMMAND)
	    if (eq(t->t_dcom[0], STRnice))
		if (t->t_dcom[1])
		    if (strchr("+-", t->t_dcom[1][0]))
			if (t->t_dcom[2]) {
			    setname("nice");
			    t->t_nice =
				getn(t->t_dcom[1]);
			    lshift(t->t_dcom, 2);
			    t->t_dflg |= F_NICE;
			}
			else
			    break;
		    else {
			t->t_nice = 4;
			lshift(t->t_dcom, 1);
			t->t_dflg |= F_NICE;
		    }
		else
		    break;
	    else if (eq(t->t_dcom[0], STRnohup))
		if (t->t_dcom[1]) {
		    t->t_dflg |= F_NOHUP;
		    lshift(t->t_dcom, 1);
		}
		else
		    break;
	    else if (eq(t->t_dcom[0], STRtime))
		if (t->t_dcom[1]) {
		    t->t_dflg |= F_TIME;
		    lshift(t->t_dcom, 1);
		}
		else
		    break;
	    else
		break;

	/* is it a command */
	if (t->t_dtyp == NODE_COMMAND) {
	    /*
	     * Check if we have a builtin function and remember which one.
	     */
	    bifunc = isbfunc(t);
	    if (noexec) {
		/*
		 * Continue for builtins that are part of the scripting language
		 */
		if (bifunc &&
		    bifunc->bfunct != dobreak   && bifunc->bfunct != docontin &&
		    bifunc->bfunct != doelse    && bifunc->bfunct != doend    &&
		    bifunc->bfunct != doforeach && bifunc->bfunct != dogoto   &&
		    bifunc->bfunct != doif      && bifunc->bfunct != dorepeat &&
		    bifunc->bfunct != doswbrk   && bifunc->bfunct != doswitch &&
		    bifunc->bfunct != dowhile   && bifunc->bfunct != dozip)
		    break;
	    }
	}
	else {			/* not a command */
	    bifunc = NULL;
	    if (noexec)
		break;
	}

	/*
	 * We fork only if we are timed, or are not the end of a parenthesized
	 * list and not a simple builtin function. Simple meaning one that is
	 * not pipedout, niced, nohupped, or &'d. It would be nice(?) to not
	 * fork in some of these cases.
	 */
	/*
	 * Prevent forking cd, pushd, popd, chdir cause this will cause the
	 * shell not to change dir!
	 */
	if (bifunc && (bifunc->bfunct == dochngd ||
		       bifunc->bfunct == dopushd ||
		       bifunc->bfunct == dopopd))
	    t->t_dflg &= ~(F_NICE);
	if (((t->t_dflg & F_TIME) || ((t->t_dflg & F_NOFORK) == 0 &&
	     (!bifunc || t->t_dflg &
	      (F_PIPEOUT | F_AMPERSAND | F_NICE | F_NOHUP)))) ||
	/*
	 * We have to fork for eval too.
	 */
	    (bifunc && (t->t_dflg & (F_PIPEIN | F_PIPEOUT)) != 0 &&
	     bifunc->bfunct == doeval)) {
	    if (t->t_dtyp == NODE_PAREN ||
		t->t_dflg & (F_REPEAT | F_AMPERSAND) || bifunc) {
		forked++;
		/*
		 * We need to block SIGCHLD/SIGHUP here, so that if the process does
		 * not die before we can set the process group
		 */
		if (wanttty >= 0 && !nosigchld) {
		    sigemptyset(&sigset);
		    sigaddset(&sigset, SIGCHLD);
		    sigaddset(&sigset, SIGHUP);
		    sigprocmask(SIG_BLOCK, &sigset, &csigset);
		    nosigchld = 1;
		}

		pid = pfork(t, wanttty);
		if (pid == 0 && nosigchld) {
		    sigprocmask(SIG_SETMASK, &csigset, NULL);
		    nosigchld = 0;
		}
		else if (pid != 0 && (t->t_dflg & F_AMPERSAND))
		    backpid = pid;

	    }
	    else {
		int     ochild, osetintr, ohaderr, odidfds;
		int     oSHIN, oSHOUT, oSHERR, oOLDSTD, otpgrp;
		sigset_t osigset;

		/*
		 * Prepare for the vfork by saving everything that the child
		 * corrupts before it exec's. Note that in some signal
		 * implementations which keep the signal info in user space
		 * (e.g. Sun's) it will also be necessary to save and restore
		 * the current sigaction's for the signals the child touches
		 * before it exec's.
		 */
		if (wanttty >= 0 && !nosigchld && !noexec) {
		    sigemptyset(&sigset);
		    sigaddset(&sigset, SIGCHLD);
		    sigaddset(&sigset, SIGHUP);
		    sigprocmask(SIG_BLOCK, &sigset, &csigset);
		    nosigchld = 1;
		}
		sigemptyset(&sigset);
		sigaddset(&sigset, SIGCHLD);
		sigaddset(&sigset, SIGHUP);
		sigaddset(&sigset, SIGINT);
		sigprocmask(SIG_BLOCK, &sigset, &osigset);
		ochild = child;
		osetintr = setintr;
		ohaderr = haderr;
		odidfds = didfds;
		oSHIN = SHIN;
		oSHOUT = SHOUT;
		oSHERR = SHERR;
		oOLDSTD = OLDSTD;
		otpgrp = tpgrp;
		ocsigset = csigset;
		onosigchld = nosigchld;
		Vsav = Vdp = NULL;
		Vexpath = 0;
		Vt = 0;
		pid = vfork();

		if (pid == -1) {
		    sigprocmask(SIG_SETMASK, &osigset, NULL);
		    stderror(ERR_NOPROC);
		}
		forked++;
		if (pid) {	/* parent */
		    child = ochild;
		    setintr = osetintr;
		    haderr = ohaderr;
		    didfds = odidfds;
		    SHIN = oSHIN;
		    SHOUT = oSHOUT;
		    SHERR = oSHERR;
		    OLDSTD = oOLDSTD;
		    tpgrp = otpgrp;
		    csigset = ocsigset;
		    nosigchld = onosigchld;

		    free(Vsav);
		    Vsav = NULL;
		    free(Vdp);
		    Vdp = NULL;
		    free(Vexpath);
		    Vexpath = NULL;
		    blkfree((Char **) Vt);
		    Vt = NULL;
		    /* this is from pfork() */
		    palloc(pid, t);
		    sigprocmask(SIG_SETMASK, &osigset, NULL);
		}
		else {		/* child */
		    /* this is from pfork() */
		    int     pgrp;
		    bool    ignint = 0;

		    if (nosigchld) {
			sigprocmask(SIG_SETMASK, &csigset, NULL);
			nosigchld = 0;
		    }

		    if (setintr)
			ignint =
			    (tpgrp == -1 &&
			     (t->t_dflg & F_NOINTERRUPT))
			    || (gointr && eq(gointr, STRminus));
		    pgrp = pcurrjob ? pcurrjob->p_jobid : getpid();
		    child++;
		    if (setintr) {
			setintr = 0;
			if (ignint) {
			    (void) signal(SIGINT, SIG_IGN);
			    (void) signal(SIGQUIT, SIG_IGN);
			}
			else {
			    (void) signal(SIGINT, vffree);
			    (void) signal(SIGQUIT, SIG_DFL);
			}

			if (wanttty >= 0) {
			    (void) signal(SIGTSTP, SIG_DFL);
			    (void) signal(SIGTTIN, SIG_DFL);
			    (void) signal(SIGTTOU, SIG_DFL);
			}

			(void) signal(SIGTERM, parterm);
		    }
		    else if (tpgrp == -1 &&
			     (t->t_dflg & F_NOINTERRUPT)) {
			(void) signal(SIGINT, SIG_IGN);
			(void) signal(SIGQUIT, SIG_IGN);
		    }

		    pgetty(wanttty, pgrp);
		    if (t->t_dflg & F_NOHUP)
			(void) signal(SIGHUP, SIG_IGN);
		    if (t->t_dflg & F_NICE)
			(void) setpriority(PRIO_PROCESS, 0, t->t_nice);
		}

	    }
	}
	if (pid != 0) {
	    /*
	     * It would be better if we could wait for the whole job when we
	     * knew the last process had been started.  Pwait, in fact, does
	     * wait for the whole job anyway, but this test doesn't really
	     * express our intentions.
	     */
	    if (didfds == 0 && t->t_dflg & F_PIPEIN) {
		(void) close(pipein[0]);
		(void) close(pipein[1]);
	    }
	    if ((t->t_dflg & F_PIPEOUT) == 0) {
		if (nosigchld) {
		    sigprocmask(SIG_SETMASK, &csigset, NULL);
		    nosigchld = 0;
		}
		if ((t->t_dflg & F_AMPERSAND) == 0)
		    pwait();
	    }
	    break;
	}
	doio(t, pipein, pipeout);
	if (t->t_dflg & F_PIPEOUT) {
	    (void) close(pipeout[0]);
	    (void) close(pipeout[1]);
	}
	/*
	 * Perform a builtin function. If we are not forked, arrange for
	 * possible stopping
	 */
	if (bifunc) {
	    func(t, bifunc);
	    if (forked)
		exitstat();
	    break;
	}
	if (t->t_dtyp != NODE_PAREN) {
	    doexec(NULL, t);
	    /* NOTREACHED */
	}
	/*
	 * For () commands must put new 0,1,2 in FSH* and recurse
	 */
	OLDSTD = dcopy(0, FOLDSTD);
	SHOUT = dcopy(1, FSHOUT);
	SHERR = dcopy(2, FSHERR);
	(void) close(SHIN);
	SHIN = -1;
	didfds = 0;
	wanttty = -1;
	t->t_dspr->t_dflg |= t->t_dflg & F_NOINTERRUPT;
	execute(t->t_dspr, wanttty, NULL, NULL);
	exitstat();

    case NODE_PIPE:
	t->t_dcar->t_dflg |= F_PIPEOUT |
	    (t->t_dflg & (F_PIPEIN | F_AMPERSAND | F_STDERR | F_NOINTERRUPT));
	execute(t->t_dcar, wanttty, pipein, pv);
	t->t_dcdr->t_dflg |= F_PIPEIN | (t->t_dflg &
			(F_PIPEOUT | F_AMPERSAND | F_NOFORK | F_NOINTERRUPT));
	if (wanttty > 0)
	    wanttty = 0;	/* got tty already */
	execute(t->t_dcdr, wanttty, pv, pipeout);
	break;

    case NODE_LIST:
	if (t->t_dcar) {
	    t->t_dcar->t_dflg |= t->t_dflg & F_NOINTERRUPT;
	    execute(t->t_dcar, wanttty, NULL, NULL);
	    /*
	     * In strange case of A&B make a new job after A
	     */
	    if (t->t_dcar->t_dflg & F_AMPERSAND && t->t_dcdr &&
		(t->t_dcdr->t_dflg & F_AMPERSAND) == 0)
		pendjob();
	}
	if (t->t_dcdr) {
	    t->t_dcdr->t_dflg |= t->t_dflg &
		(F_NOFORK | F_NOINTERRUPT);
	    execute(t->t_dcdr, wanttty, NULL, NULL);
	}
	break;

    case NODE_OR:
    case NODE_AND:
	if (t->t_dcar) {
	    t->t_dcar->t_dflg |= t->t_dflg & F_NOINTERRUPT;
	    execute(t->t_dcar, wanttty, NULL, NULL);
	    if ((getn(value(STRstatus)) == 0) !=
		(t->t_dtyp == NODE_AND))
		return;
	}
	if (t->t_dcdr) {
	    t->t_dcdr->t_dflg |= t->t_dflg &
		(F_NOFORK | F_NOINTERRUPT);
	    execute(t->t_dcdr, wanttty, NULL, NULL);
	}
	break;
    }
    /*
     * Fall through for all breaks from switch
     *
     * If there will be no more executions of this command, flush all file
     * descriptors. Places that turn on the F_REPEAT bit are responsible for
     * doing donefds after the last re-execution
     */
    if (didfds && !(t->t_dflg & F_REPEAT))
	donefds();
}

static void
vffree(int i)
{
    _exit(i);
}

/*
 * Expand and glob the words after an i/o redirection.
 * If more than one word is generated, then update the command vector.
 *
 * This is done differently in all the shells:
 * 1. in the bourne shell and ksh globbing is not performed
 * 2. Bash/csh say ambiguous
 * 3. zsh does i/o to/from all the files
 * 4. itcsh concatenates the words.
 *
 * I don't know what is best to do. I think that Ambiguous is better
 * than restructuring the command vector, because the user can get
 * unexpected results. In any case, the command vector restructuring
 * code is present and the user can choose it by setting noambiguous
 */
static Char *
splicepipe(struct command *t, Char *cp) /* word after < or > */
{
    Char *blk[2];

    if (adrof(STRnoambiguous)) {
	Char **pv;

	blk[0] = Dfix1(cp); /* expand $ */
	blk[1] = NULL;

	gflag = 0, tglob(blk);
	if (gflag) {
	    pv = globall(blk);
	    if (pv == NULL) {
		setname(vis_str(blk[0]));
		free(blk[0]);
		stderror(ERR_NAME | ERR_NOMATCH);
	    }
	    gargv = NULL;
	    if (pv[1] != NULL) { /* we need to fix the command vector */
		Char **av = blkspl(t->t_dcom, &pv[1]);
		free(t->t_dcom);
		t->t_dcom = av;
	    }
	    free(blk[0]);
	    blk[0] = pv[0];
	    free(pv);
	}
    }
    else {
	blk[0] = globone(blk[1] = Dfix1(cp), G_ERROR);
	free(blk[1]);
    }
    return(blk[0]);
}

/*
 * Perform io redirection.
 * We may or maynot be forked here.
 */
static void
doio(struct command *t, int *pipein, int *pipeout)
{
    int fd;
    Char *cp;
    int flags = t->t_dflg;

    if (didfds || (flags & F_REPEAT))
	return;
    if ((flags & F_READ) == 0) {/* F_READ already done */
	if (t->t_dlef) {
	    char    tmp[PATH_MAX];

	    /*
	     * so < /dev/std{in,out,err} work
	     */
	    (void) dcopy(SHIN, 0);
	    (void) dcopy(SHOUT, 1);
	    (void) dcopy(SHERR, 2);
	    cp = splicepipe(t, t->t_dlef);
	    strlcpy(tmp, short2str(cp), sizeof tmp);
	    free(cp);
	    if ((fd = open(tmp, O_RDONLY)) == -1)
		stderror(ERR_SYSTEM, tmp, strerror(errno));
	    (void) dmove(fd, 0);
	}
	else if (flags & F_PIPEIN) {
	    (void) dup2(pipein[0], 0);
	    (void) close(pipein[0]);
	    (void) close(pipein[1]);
	}
	else if ((flags & F_NOINTERRUPT) && tpgrp == -1) {
	    (void) close(0);
	    (void) open(_PATH_DEVNULL, O_RDONLY);
	}
	else {
	    (void) dup2(OLDSTD, 0);
	}
    }
    if (t->t_drit) {
	char    tmp[PATH_MAX];

	cp = splicepipe(t, t->t_drit);
	strlcpy(tmp, short2str(cp), sizeof tmp);
	free(cp);
	/*
	 * so > /dev/std{out,err} work
	 */
	(void) dcopy(SHOUT, 1);
	(void) dcopy(SHERR, 2);
	if ((flags & F_APPEND) &&
	    (fd = open(tmp, O_WRONLY | O_APPEND)) >= 0);
	else {
	    if (!(flags & F_OVERWRITE) && adrof(STRnoclobber)) {
		if (flags & F_APPEND)
		    stderror(ERR_SYSTEM, tmp, strerror(errno));
		chkclob(tmp);
	    }
	    if ((fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1)
		stderror(ERR_SYSTEM, tmp, strerror(errno));
	}
	(void) dmove(fd, 1);
    }
    else if (flags & F_PIPEOUT) {
	(void) dup2(pipeout[1], 1);
    }
    else {
	(void) dup2(SHOUT, 1);
    }

    if (flags & F_STDERR) {
	(void) dup2(1, 2);
    }
    else {
	(void) dup2(SHERR, 2);
    }
    didfds = 1;
}

void
mypipe(int *pv)
{

    if (pipe(pv) == -1)
	goto oops;
    pv[0] = dmove(pv[0], -1);
    pv[1] = dmove(pv[1], -1);
    if (pv[0] >= 0 && pv[1] >= 0)
	return;
oops:
    stderror(ERR_PIPE);
}

static void
chkclob(char *cp)
{
    struct stat stb;

    if (stat(cp, &stb) == -1)
	return;
    if (S_ISCHR(stb.st_mode))
	return;
    stderror(ERR_EXISTS, cp);
}
