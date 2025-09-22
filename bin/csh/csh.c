/*	$OpenBSD: csh.c,v 1.51 2024/07/28 15:31:22 deraadt Exp $	*/
/*	$NetBSD: csh.c,v 1.14 1995/04/29 23:21:28 mycroft Exp $	*/

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
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <unistd.h>
#include <limits.h>
#include <vis.h>
#include <stdarg.h>

#include "csh.h"
#include "proc.h"
#include "extern.h"
#include "pathnames.h"

/*
 * C Shell
 *
 * Bill Joy, UC Berkeley, California, USA
 * October 1978, May 1980
 *
 * Jim Kulp, IIASA, Laxenburg, Austria
 * April 1980
 *
 * Christos Zoulas, Cornell University
 * June, 1991
 */

/* Globals from csh.h */
struct timespec time0;
struct rusage ru0;
struct Bin B;
struct Ain lineloc;
struct whyle *whyles;
struct varent shvhed, aliases;
struct wordent *alhistp;
struct wordent *alhistt;
struct wordent paraml;
struct Hist Histlist;
FILE   *cshin, *cshout, *csherr;
bool    chkstop;
bool    didfds;
bool    doneinp;
bool    exiterr;
bool    child;
bool    haderr;
bool    intty;
bool    intact;
bool    justpr;
bool    loginsh;
bool    neednote;
bool    noexec;
bool    pjobs;
bool    setintr;
bool    timflg;
bool    havhash;
bool    needprompt;
Char   *arginp;
int     onelflg;
Char   *ffile;
Char   *doldol;
int     backpid;
uid_t   uid, euid;
gid_t   gid, egid;
time_t  chktim;
pid_t   shpgrp;
pid_t   tpgrp;
pid_t   opgrp;
int     SHIN;
int     SHOUT;
int     SHERR;
int     OLDSTD;
jmp_buf reslab;
int     exitset;
Char   *gointr;
sig_t   parintr;
sig_t   parterm;
bool    cantell;
Char   *lap;
Char  **alvec;
int     gflag;
Char   *pargs;
long    pnleft;
Char   *pargcp;
int     eventno;
int     lastev;
Char    HIST;
Char    HISTSUB;
char   *bname;
Char   *Vsav;
Char   *Vdp;
Char   *Vexpath;
char  **Vt;
Char  **evalvec;
Char   *evalp;
Char   *word_chars;
Char   *STR_SHELLPATH;
Char   *STR_BSHELL;
Char   *STR_WORD_CHARS;
Char  **STR_environ;

/* Locals */
Char   *dumphist[] = {STRhistory, STRmh, 0, 0};
Char   *loadhist[] = {STRsource, STRmh, STRtildothist, 0};

int     nofile = 0;
bool    reenter = 0;
bool    nverbose = 0;
bool    nexececho = 0;
bool    quitit = 0;
bool    fast = 0;
bool    batch = 0;
bool    mflag = 0;
bool    prompt = 1;
bool    enterhist = 0;
bool    tellwhat = 0;

extern char **environ;

static int	readf(void *, char *, int);
static off_t	seekf(void *, off_t, int);
static int	writef(void *, const char *, int);
static int	closef(void *);
static int	srccat(Char *, Char *);
static int	srcfile(char *, bool, bool);
static void	phup(int);
static void	srcunit(int, bool, bool);
static void	mailchk(void);
static Char   **defaultpath(void);

int
main(int argc, char *argv[])
{
    Char *cp;
    char *tcp;
    int f;
    char **tempv;
    struct sigaction oact;
    sigset_t sigset;

    cshin = stdin;
    cshout = stdout;
    csherr = stderr;

    settimes();			/* Immed. estab. timing base */

    /*
     * Initialize non constant strings
     */
    STR_BSHELL = SAVE(_PATH_BSHELL);
    STR_SHELLPATH = SAVE(_PATH_CSHELL);
    STR_environ = blk2short(environ);
    environ = short2blk(STR_environ);	/* So that we can free it */
    STR_WORD_CHARS = SAVE(WORD_CHARS);

    HIST = '!';
    HISTSUB = '^';
    word_chars = STR_WORD_CHARS;

    tempv = argv;
    if (eq(str2short(tempv[0]), STRaout))	/* A.out's are quittable */
	quitit = 1;
    uid = getuid();
    gid = getgid();
    euid = geteuid();
    egid = getegid();
    /*
     * We are a login shell if: 1. we were invoked as -<something> and we had
     * no arguments 2. or we were invoked only with the -l flag
     */
    loginsh = (**tempv == '-' && argc == 1) ||
	(argc == 2 && tempv[1][0] == '-' && tempv[1][1] == 'l' &&
	 tempv[1][2] == '\0');

    if (loginsh && **tempv != '-') {
	/*
	 * Mangle the argv space
	 */
	tempv[1][0] = '\0';
	tempv[1][1] = '\0';
	tempv[1] = NULL;
	for (tcp = *tempv; *tcp++;)
	    continue;
	for (tcp--; tcp >= *tempv; tcp--)
	    tcp[1] = tcp[0];
	*++tcp = '-';
	argc--;
    }
    if (loginsh)
	(void) time(&chktim);

    if (pledge("stdio rpath wpath cpath fattr getpw proc exec tty",
	NULL) == -1) {
	    perror("pledge");
	    exit(1);
    }

    /*
     * Move the descriptors to safe places. The variable didfds is 0 while we
     * have only FSH* to work with. When didfds is true, we have 0,1,2 and
     * prefer to use these.
     */
    initdesc();
    /*
     * XXX: This is to keep programs that use stdio happy.
     *	    what we really want is freunopen() ....
     *	    Closing cshin cshout and csherr (which are really stdin stdout
     *	    and stderr at this point and then reopening them in the same order
     *	    gives us again stdin == cshin stdout == cshout and stderr == csherr.
     *	    If that was not the case builtins like printf that use stdio
     *	    would break. But in any case we could fix that with memcpy and
     *	    a bit of pointer manipulation...
     *	    Fortunately this is not needed under the current implementation
     *	    of stdio.
     */
    (void) fclose(cshin);
    (void) fclose(cshout);
    (void) fclose(csherr);
    if (!(cshin  = funopen((void *) &SHIN,  readf, writef, seekf, closef)))
	exit(1);
    if (!(cshout = funopen((void *) &SHOUT, readf, writef, seekf, closef)))
	exit(1);
    if (!(csherr = funopen((void *) &SHERR, readf, writef, seekf, closef)))
	exit(1);
    (void) setvbuf(cshin,  NULL, _IOLBF, 0);
    (void) setvbuf(cshout, NULL, _IOLBF, 0);
    (void) setvbuf(csherr, NULL, _IOLBF, 0);

    /*
     * Initialize the shell variables. ARGV and PROMPT are initialized later.
     * STATUS is also munged in several places. CHILD is munged when
     * forking/waiting
     */
    set(STRstatus, Strsave(STR0));

    if ((tcp = getenv("HOME")) != NULL && strlen(tcp) < PATH_MAX)
	cp = SAVE(tcp);
    else
	cp = NULL;

    if (cp == NULL)
	fast = 1;		/* No home -> can't read scripts */
    else
	set(STRhome, cp);
    dinit(cp);			/* dinit thinks that HOME == cwd in a login
				 * shell */
    /*
     * Grab other useful things from the environment. Should we grab
     * everything??
     */
    if ((tcp = getenv("LOGNAME")) != NULL ||
	(tcp = getenv("USER")) != NULL)
	set(STRuser, quote(SAVE(tcp)));
    if ((tcp = getenv("TERM")) != NULL)
	set(STRterm, quote(SAVE(tcp)));

    /*
     * Re-initialize path if set in environment
     */
    if ((tcp = getenv("PATH")) == NULL)
	setq(STRpath, defaultpath(), &shvhed);
    else
	importpath(str2short(tcp));

    set(STRshell, Strsave(STR_SHELLPATH));

    doldol = putn((int) getpid());	/* For $$ */

    /*
     * Record the interrupt states from the parent process. If the parent is
     * non-interruptible our hand must be forced or we (and our children) won't
     * be either. Our children inherit termination from our parent. We catch it
     * only if we are the login shell.
     */
    /* parents interruptibility */
    (void) sigaction(SIGINT, NULL, &oact);
    parintr = oact.sa_handler;
    (void) sigaction(SIGTERM, NULL, &oact);
    parterm = oact.sa_handler;

    /* catch these all, login shell or not */
    (void) signal(SIGHUP, phup);	/* exit processing on HUP */
    (void) signal(SIGXCPU, phup);	/* ...and on XCPU */
    (void) signal(SIGXFSZ, phup);	/* ...and on XFSZ */

    /*
     * Process the arguments.
     *
     * Note that processing of -v/-x is actually delayed till after script
     * processing.
     *
     * We set the first character of our name to be '-' if we are a shell
     * running interruptible commands.  Many programs which examine ps'es
     * use this to filter such shells out.
     */
    argc--, tempv++;
    while (argc > 0 && (tcp = tempv[0])[0] == '-' && *++tcp != '\0' && !batch) {
	do
	    switch (*tcp++) {

	    case 0:		/* -	Interruptible, no prompt */
		prompt = 0;
		setintr = 1;
		nofile = 1;
		break;

	    case 'b':		/* -b	Next arg is input file */
		batch = 1;
		break;

	    case 'c':		/* -c	Command input from arg */
		if (argc == 1)
		    xexit(0);
		argc--, tempv++;
		arginp = SAVE(tempv[0]);
		prompt = 0;
		nofile = 1;
		break;

	    case 'e':		/* -e	Exit on any error */
		exiterr = 1;
		break;

	    case 'f':		/* -f	Fast start */
		fast = 1;
		break;

	    case 'i':		/* -i	Interactive, even if !intty */
		intact = 1;
		nofile = 1;
		break;

	    case 'm':		/* -m	read .cshrc (from su) */
		mflag = 1;
		break;

	    case 'n':		/* -n	Don't execute */
		noexec = 1;
		break;

	    case 'q':		/* -q	(Undoc'd) ... die on quit */
		quitit = 1;
		break;

	    case 's':		/* -s	Read from std input */
		nofile = 1;
		break;

	    case 't':		/* -t	Read one line from input */
		onelflg = 2;
		prompt = 0;
		nofile = 1;
		break;

	    case 'v':		/* -v	Echo hist expanded input */
		nverbose = 1;	/* ... later */
		break;

	    case 'x':		/* -x	Echo just before execution */
		nexececho = 1;	/* ... later */
		break;

	    case 'V':		/* -V	Echo hist expanded input */
		setNS(STRverbose);	/* NOW! */
		break;

	    case 'X':		/* -X	Echo just before execution */
		setNS(STRecho);	/* NOW! */
		break;

	} while (*tcp);
	tempv++, argc--;
    }

    if (quitit)			/* With all due haste, for debugging */
	(void) signal(SIGQUIT, SIG_DFL);

    /*
     * Unless prevented by -, -c, -i, -s, or -t, if there are remaining
     * arguments the first of them is the name of a shell file from which to
     * read commands.
     */
    if (nofile == 0 && argc > 0) {
	nofile = open(tempv[0], O_RDONLY);
	if (nofile == -1) {
	    child = 1;		/* So this doesn't return */
	    stderror(ERR_SYSTEM, tempv[0], strerror(errno));
	}
	ffile = SAVE(tempv[0]);
	/*
	 * Replace FSHIN. Handle /dev/std{in,out,err} specially
	 * since once they are closed we cannot open them again.
	 * In that case we use our own saved descriptors
	 */
	if ((SHIN = dmove(nofile, FSHIN)) < 0)
	    switch(nofile) {
	    case 0:
		SHIN = FSHIN;
		break;
	    case 1:
		SHIN = FSHOUT;
		break;
	    case 2:
		SHIN = FSHERR;
		break;
	    default:
		stderror(ERR_SYSTEM, tempv[0], strerror(errno));
		break;
	    }
	(void) fcntl(SHIN, F_SETFD, FD_CLOEXEC);
	prompt = 0;
	 /* argc not used any more */ tempv++;
    }

    intty = isatty(SHIN);
    intty |= intact;
    if (intty || (intact && isatty(SHOUT))) {
	if (!batch && (uid != euid || gid != egid)) {
	    errno = EACCES;
	    child = 1;		/* So this doesn't return */
	    stderror(ERR_SYSTEM, "csh", strerror(errno));
	}
    }
    /*
     * Decide whether we should play with signals or not. If we are explicitly
     * told (via -i, or -) or we are a login shell (arg0 starts with -) or the
     * input and output are both the ttys("csh", or "csh</dev/ttyx>/dev/ttyx")
     * Note that in only the login shell is it likely that parent may have set
     * signals to be ignored
     */
    if (loginsh || intact || (intty && isatty(SHOUT)))
	setintr = 1;
    settell();
    /*
     * Save the remaining arguments in argv.
     */
    setq(STRargv, blk2short(tempv), &shvhed);

    /*
     * Set up the prompt.
     */
    if (prompt) {
	set(STRprompt, Strsave(uid == 0 ? STRpromptroot : STRpromptuser));
	/* that's a meta-questionmark */
	set(STRprompt2, Strsave(STRmquestion));
    }

    /*
     * If we are an interactive shell, then start fiddling with the signals;
     * this is a tricky game.
     */
    shpgrp = getpgrp();
    opgrp = tpgrp = -1;
    if (setintr) {
	**argv = '-';
	if (!quitit)		/* Wary! */
	    (void) signal(SIGQUIT, SIG_IGN);
	(void) signal(SIGINT, pintr);
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigprocmask(SIG_BLOCK, &sigset, NULL);
	(void) signal(SIGTERM, SIG_IGN);
	if (quitit == 0 && arginp == 0) {
	    (void) signal(SIGTSTP, SIG_IGN);
	    (void) signal(SIGTTIN, SIG_IGN);
	    (void) signal(SIGTTOU, SIG_IGN);
	    /*
	     * Wait till in foreground, in case someone stupidly runs csh &
	     * dont want to try to grab away the tty.
	     */
	    if (isatty(FSHERR))
		f = FSHERR;
	    else if (isatty(FSHOUT))
		f = FSHOUT;
	    else if (isatty(OLDSTD))
		f = OLDSTD;
	    else
		f = -1;
    retry:
	    if ((tpgrp = tcgetpgrp(f)) != -1) {
		if (tpgrp != shpgrp) {
		    sig_t old = signal(SIGTTIN, SIG_DFL);
		    (void) kill(0, SIGTTIN);
		    (void) signal(SIGTTIN, old);
		    goto retry;
		}
		opgrp = shpgrp;
		shpgrp = getpid();
		tpgrp = shpgrp;
		/*
		 * Setpgid will fail if we are a session leader and
		 * mypid == mypgrp (POSIX 4.3.3)
		 */
		if (opgrp != shpgrp)
		    if (setpgid(0, shpgrp) == -1)
			goto notty;
		/*
		 * We do that after we set our process group, to make sure
		 * that the process group belongs to a process in the same
		 * session as the tty (our process and our group) (POSIX 7.2.4)
		 */
		if (tcsetpgrp(f, shpgrp) == -1)
		    goto notty;
		(void) fcntl(dcopy(f, FSHTTY), F_SETFD, FD_CLOEXEC);
	    }
	    if (tpgrp == -1) {
notty:
		(void) fprintf(csherr, "Warning: no access to tty (%s).\n",
			       strerror(errno));
		(void) fprintf(csherr, "Thus no job control in this shell.\n");
	    }
	}
    }
    if ((setintr == 0) && (parintr == SIG_DFL))
	setintr = 1;
    (void) signal(SIGCHLD, pchild);	/* while signals not ready */

    /*
     * Set an exit here in case of an interrupt or error reading the shell
     * start-up scripts.
     */
    reenter = setexit();	/* PWP */
    exitset++;
    haderr = 0;			/* In case second time through */
    if (!fast && reenter == 0) {
	/* Will have value(STRhome) here because set fast if don't */
	{
	    int     osetintr = setintr;
	    sig_t   oparintr = parintr;
	    sigset_t osigset;

	    sigemptyset(&sigset);
	    sigaddset(&sigset, SIGINT);
	    sigprocmask(SIG_BLOCK, &sigset, &osigset);

	    setintr = 0;
	    parintr = SIG_IGN;	/* Disable onintr */
	    (void) srcfile(_PATH_DOTCSHRC, 0, 0);
	    if (!fast && !arginp && !onelflg)
		dohash(NULL, NULL);
	    if (loginsh)
		(void) srcfile(_PATH_DOTLOGIN, 0, 0);
	    sigprocmask(SIG_SETMASK, &osigset, NULL);
	    setintr = osetintr;
	    parintr = oparintr;
	}
	(void) srccat(value(STRhome), STRsldotcshrc);

	if (!fast && !arginp && !onelflg && !havhash)
	    dohash(NULL, NULL);
	/*
	 * Source history before .login so that it is available in .login
	 */
	if ((cp = value(STRhistfile)) != STRNULL)
	    loadhist[2] = cp;
	dosource(loadhist, NULL);
	if (loginsh)
	      (void) srccat(value(STRhome), STRsldotlogin);
    }

    /*
     * Now are ready for the -v and -x flags
     */
    if (nverbose)
	setNS(STRverbose);
    if (nexececho)
	setNS(STRecho);

    /*
     * All the rest of the world is inside this call. The argument to process
     * indicates whether it should catch "error unwinds".  Thus if we are a
     * interactive shell our call here will never return by being blown past on
     * an error.
     */
    process(setintr);

    /*
     * Mop-up.
     */
    if (intty) {
	if (loginsh) {
	    (void) fprintf(cshout, "logout\n");
	    (void) close(SHIN);
	    child = 1;
	    goodbye();
	}
	else {
	    (void) fprintf(cshout, "exit\n");
	}
    }
    rechist();
    exitstat();
    return (0);
}

void
untty(void)
{
    if (tpgrp > 0) {
	(void) setpgid(0, opgrp);
	(void) tcsetpgrp(FSHTTY, opgrp);
    }
}

void
importpath(Char *cp)
{
    int i = 0;
    Char *dp;
    Char **pv;
    int     c;

    for (dp = cp; *dp; dp++)
	if (*dp == ':')
	    i++;
    /*
     * i+2 where i is the number of colons in the path. There are i+1
     * directories in the path plus we need room for a zero terminator.
     */
    pv = xcalloc(i + 2, sizeof(*pv));
    dp = cp;
    i = 0;
    if (*dp)
	for (;;) {
	    if ((c = *dp) == ':' || c == 0) {
		*dp = 0;
		pv[i++] = Strsave(*cp ? cp : STRdot);
		if (c) {
		    cp = dp + 1;
		    *dp = ':';
		}
		else
		    break;
	    }
	    dp++;
	}
    pv[i] = 0;
    setq(STRpath, pv, &shvhed);
}

/*
 * Source to the file which is the catenation of the argument names.
 */
static int
srccat(Char *cp, Char *dp)
{
    Char *ep = Strspl(cp, dp);
    char   *ptr = short2str(ep);

    free(ep);
    return srcfile(ptr, mflag ? 0 : 1, 0);
}

/*
 * Source to a file putting the file descriptor in a safe place (> 2).
 */
static int
srcfile(char *f, bool onlyown, bool flag)
{
    int unit;

    if ((unit = open(f, O_RDONLY)) == -1)
	return 0;
    unit = dmove(unit, -1);

    (void) fcntl(unit, F_SETFD, FD_CLOEXEC);
    srcunit(unit, onlyown, flag);
    return 1;
}

/*
 * Source to a unit.  If onlyown it must be our file or our group or
 * we don't chance it.	This occurs on ".cshrc"s and the like.
 */
int     insource;
static void
srcunit(int unit, bool onlyown, bool hflg)
{
    /* We have to push down a lot of state here */
    /* All this could go into a structure */
    int     oSHIN = -1, oldintty = intty, oinsource = insource;
    struct whyle *oldwhyl = whyles;
    Char   *ogointr = gointr, *oarginp = arginp;
    Char   *oevalp = evalp, **oevalvec = evalvec;
    int     oonelflg = onelflg;
    bool    oenterhist = enterhist;
    char    OHIST = HIST;
    bool    otell = cantell;

    struct Bin saveB;
    sigset_t sigset, osigset;
    jmp_buf oldexit;

    /* The (few) real local variables */
    int     my_reenter;

    if (unit < 0)
	return;
    if (didfds)
	donefds();
    if (onlyown) {
	struct stat stb;

	if (fstat(unit, &stb) == -1) {
	    (void) close(unit);
	    return;
	}
    }

    /*
     * There is a critical section here while we are pushing down the input
     * stream since we have stuff in different structures. If we weren't
     * careful an interrupt could corrupt SHIN's Bin structure and kill the
     * shell.
     *
     * We could avoid the critical region by grouping all the stuff in a single
     * structure and pointing at it to move it all at once.  This is less
     * efficient globally on many variable references however.
     */
    insource = 1;
    getexit(oldexit);

    if (setintr) {
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigprocmask(SIG_BLOCK, &sigset, &osigset);
    }
    /* Setup the new values of the state stuff saved above */
    memcpy(&saveB, &B, sizeof(B));
    fbuf = NULL;
    fseekp = feobp = fblocks = 0;
    oSHIN = SHIN, SHIN = unit, arginp = 0, onelflg = 0;
    intty = isatty(SHIN), whyles = 0, gointr = 0;
    evalvec = 0;
    evalp = 0;
    enterhist = hflg;
    if (enterhist)
	HIST = '\0';

    /*
     * Now if we are allowing commands to be interrupted, we let ourselves be
     * interrupted.
     */
    if (setintr)
	sigprocmask(SIG_SETMASK, &osigset, NULL);
    settell();

    if ((my_reenter = setexit()) == 0)
	process(0);		/* 0 -> blow away on errors */

    if (setintr)
	sigprocmask(SIG_SETMASK, &osigset, NULL);
    if (oSHIN >= 0) {
	int i;

	/* We made it to the new state... free up its storage */
	/* This code could get run twice but free doesn't care */
	for (i = 0; i < fblocks; i++)
	    free(fbuf[i]);
	free(fbuf);

	/* Reset input arena */
	memcpy(&B, &saveB, sizeof(B));

	(void) close(SHIN), SHIN = oSHIN;
	arginp = oarginp, onelflg = oonelflg;
	evalp = oevalp, evalvec = oevalvec;
	intty = oldintty, whyles = oldwhyl, gointr = ogointr;
	if (enterhist)
	    HIST = OHIST;
	enterhist = oenterhist;
	cantell = otell;
    }

    resexit(oldexit);
    /*
     * If process reset() (effectively an unwind) then we must also unwind.
     */
    if (my_reenter)
	stderror(ERR_SILENT);
    insource = oinsource;
}

void
rechist(void)
{
    Char    buf[BUFSIZ], hbuf[BUFSIZ], *hfile;
    int     fd, ftmp, oldidfds;
    struct  varent *shist;

    if (!fast) {
	/*
	 * If $savehist is just set, we use the value of $history
	 * else we use the value in $savehist
	 */
	if ((shist = adrof(STRsavehist)) != NULL) {
	    if (shist->vec[0][0] != '\0')
		(void) Strlcpy(hbuf, shist->vec[0], sizeof hbuf/sizeof(Char));
	    else if ((shist = adrof(STRhistory)) && shist->vec[0][0] != '\0')
		(void) Strlcpy(hbuf, shist->vec[0], sizeof hbuf/sizeof(Char));
	    else
		return;
	}
	else
	    return;

	if ((hfile = value(STRhistfile)) == STRNULL) {
	    Strlcpy(buf, value(STRhome), sizeof buf/sizeof(Char));
	    hfile = buf;
	    (void) Strlcat(buf, STRsldthist, sizeof buf/sizeof(Char));
	}

	if ((fd = open(short2str(hfile), O_WRONLY | O_CREAT | O_TRUNC,
	    0600)) == -1)
	    return;

	oldidfds = didfds;
	didfds = 0;
	ftmp = SHOUT;
	SHOUT = fd;
	dumphist[2] = hbuf;
	dohist(dumphist, NULL);
	SHOUT = ftmp;
	(void) close(fd);
	didfds = oldidfds;
    }
}

void
goodbye(void)
{
    rechist();

    if (loginsh) {
	(void) signal(SIGQUIT, SIG_IGN);
	(void) signal(SIGINT, SIG_IGN);
	(void) signal(SIGTERM, SIG_IGN);
	setintr = 0;		/* No interrupts after "logout" */
	if (!(adrof(STRlogout)))
	    set(STRlogout, STRnormal);
	(void) srcfile(_PATH_DOTLOGOUT, 0, 0);
	if (adrof(STRhome))
	    (void) srccat(value(STRhome), STRsldtlogout);
    }
    exitstat();
}

void
exitstat(void)
{
    Char *s;
    /*
     * Note that if STATUS is corrupted (i.e. getn bombs) then error will exit
     * directly because we poke child here. Otherwise we might continue
     * unwarrantedly (sic).
     */
    child = 1;
    s = value(STRstatus);
    xexit(s ? getn(s) : 0);
}

/*
 * in the event of a HUP we want to save the history
 */
static void
phup(int sig)
{
    rechist();	/* XXX big signal race */

    /*
     * We kill the last foreground process group. It then becomes
     * responsible to propagate the SIGHUP to its progeny.
     */
    {
	struct process *pp, *np;

	for (pp = proclist.p_next; pp; pp = pp->p_next) {
	    np = pp;
	    /*
	     * Find if this job is in the foreground. It could be that
	     * the process leader has exited and the foreground flag
	     * is cleared for it.
	     */
	    do
		/*
		 * If a process is in the foreground; we try to kill
		 * its process group. If we succeed, then the
		 * whole job is gone. Otherwise we keep going...
		 * But avoid sending HUP to the shell again.
		 */
		if ((np->p_flags & PFOREGND) != 0 && np->p_jobid != shpgrp &&
		    kill(-np->p_jobid, SIGHUP) != -1) {
		    /* In case the job was suspended... */
		    (void) kill(-np->p_jobid, SIGCONT);
		    break;
		}
	    while ((np = np->p_friends) != pp);
	}
    }
    xexit(sig);
}

Char   *jobargv[2] = {STRjobs, 0};

/*
 * Catch an interrupt, e.g. during lexical input.
 * If we are an interactive shell, we reset the interrupt catch
 * immediately.  In any case we drain the shell output,
 * and finally go through the normal error mechanism, which
 * gets a chance to make the shell go away.
 */
void
pintr(int notused)
{
    int save_errno = errno;

    pintr1(1);
    errno = save_errno;
}

void
pintr1(bool wantnl)
{
    sigset_t sigset, osigset;

    sigemptyset(&sigset);
    sigprocmask(SIG_BLOCK, &sigset, &osigset);
    if (setintr) {
	sigset = osigset;
	sigdelset(&sigset, SIGINT);
	sigprocmask(SIG_SETMASK, &sigset, NULL);
	if (pjobs) {
	    pjobs = 0;
	    (void) fprintf(cshout, "\n");
	    dojobs(jobargv, NULL);
	    stderror(ERR_NAME | ERR_INTR);
	}
    }
    sigdelset(&osigset, SIGCHLD);
    sigprocmask(SIG_SETMASK, &osigset, NULL);
    (void) fpurge(cshout);
    (void) endpwent();

    /*
     * If we have an active "onintr" then we search for the label. Note that if
     * one does "onintr -" then we shan't be interruptible so we needn't worry
     * about that here.
     */
    if (gointr) {
	gotolab(gointr);
	timflg = 0;
	blkfree(pargv);
	pargv = NULL;
	blkfree(gargv);
	gargv = NULL;
	reset();
    }
    else if (intty && wantnl) {
	(void) fputc('\r', cshout);
	(void) fputc('\n', cshout);
    }
    stderror(ERR_SILENT);
}

/*
 * Process is the main driving routine for the shell.
 * It runs all command processing, except for those within { ... }
 * in expressions (which is run by a routine evalav in sh.exp.c which
 * is a stripped down process), and `...` evaluation which is run
 * also by a subset of this code in sh.glob.c in the routine backeval.
 *
 * The code here is a little strange because part of it is interruptible
 * and hence freeing of structures appears to occur when none is necessary
 * if this is ignored.
 *
 * Note that if catch is not set then we will unwind on any error.
 * If an end-of-file occurs, we return.
 */
static struct command *savet = NULL;
void
process(bool catch)
{
    jmp_buf osetexit;
    struct command *t = savet;
    sigset_t sigset;

    savet = NULL;
    getexit(osetexit);
    for (;;) {
	pendjob();
	paraml.next = paraml.prev = &paraml;
	paraml.word = STRNULL;
	(void) setexit();
	justpr = enterhist;	/* execute if not entering history */

	/*
	 * Interruptible during interactive reads
	 */
	if (setintr) {
	    sigemptyset(&sigset);
	    sigaddset(&sigset, SIGINT);
	    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
	}

	/*
	 * For the sake of reset()
	 */
	freelex(&paraml);
	if (savet)
	    freesyn(savet), savet = NULL;

	if (haderr) {
	    if (!catch) {
		/* unwind */
		doneinp = 0;
		resexit(osetexit);
		savet = t;
		reset();
	    }
	    haderr = 0;
	    /*
	     * Every error is eventually caught here or the shell dies.  It is
	     * at this point that we clean up any left-over open files, by
	     * closing all but a fixed number of pre-defined files.  Thus
	     * routines don't have to worry about leaving files open due to
	     * deeper errors... they will get closed here.
	     */
	    closem();
	    continue;
	}
	if (doneinp) {
	    doneinp = 0;
	    break;
	}
	if (chkstop)
	    chkstop--;
	if (neednote)
	    pnote();
	if (intty && prompt && evalvec == 0) {
	    mailchk();
	    /*
	     * If we are at the end of the input buffer then we are going to
	     * read fresh stuff. Otherwise, we are rereading input and don't
	     * need or want to prompt.
	     */
	    needprompt = aret == F_SEEK && fseekp == feobp;
	    if (!filec && needprompt)
		printprompt();
	    (void) fflush(cshout);
	}
	free(seterr);
	seterr = NULL;

	/*
	 * Echo not only on VERBOSE, but also with history expansion. If there
	 * is a lexical error then we forego history echo.
	 */
	if ((lex(&paraml) && !seterr && intty) || adrof(STRverbose)) {
	    prlex(csherr, &paraml);
	}

	/*
	 * The parser may lose space if interrupted.
	 */
	if (setintr)
	    sigprocmask(SIG_BLOCK, &sigset, NULL);

	/*
	 * Save input text on the history list if reading in old history, or it
	 * is from the terminal at the top level and not in a loop.
	 *
	 * PWP: entry of items in the history list while in a while loop is done
	 * elsewhere...
	 */
	if (enterhist || (catch && intty && !whyles))
	    savehist(&paraml);

	/*
	 * Print lexical error messages, except when sourcing history lists.
	 */
	if (!enterhist && seterr)
	    stderror(ERR_OLD);

	/*
	 * If had a history command :p modifier then this is as far as we
	 * should go
	 */
	if (justpr)
	    reset();

	alias(&paraml);

	/*
	 * Parse the words of the input into a parse tree.
	 */
	savet = syntax(paraml.next, &paraml, 0);
	if (seterr)
	    stderror(ERR_OLD);

	execute(savet, (tpgrp > 0 ? tpgrp : -1), NULL, NULL);

	/*
	 * Made it!
	 */
	freelex(&paraml);
	freesyn((struct command *) savet), savet = NULL;
    }
    resexit(osetexit);
    savet = t;
}

void
dosource(Char **v, struct command *t)
{
    Char *f;
    bool    hflg = 0;
    Char    buf[BUFSIZ];
    char    sbuf[BUFSIZ];

    v++;
    if (*v && eq(*v, STRmh)) {
	if (*++v == NULL)
	    stderror(ERR_NAME | ERR_HFLAG);
	hflg++;
    }
    (void) Strlcpy(buf, *v, sizeof buf/sizeof(Char));
    f = globone(buf, G_ERROR);
    (void) strlcpy(sbuf, short2str(f), sizeof sbuf);
    free(f);
    if (!srcfile(sbuf, 0, hflg) && !hflg)
	stderror(ERR_SYSTEM, sbuf, strerror(errno));
}

/*
 * Check for mail.
 * If we are a login shell, then we don't want to tell
 * about any mail file unless its been modified
 * after the time we started.
 * This prevents us from telling the user things he already
 * knows, since the login program insists on saying
 * "You have mail."
 */
static void
mailchk(void)
{
    struct varent *v;
    Char **vp;
    time_t  t;
    int     intvl, cnt;
    struct stat stb;
    bool    new;

    v = adrof(STRmail);
    if (v == 0)
	return;
    (void) time(&t);
    vp = v->vec;
    cnt = blklen(vp);
    intvl = (cnt && number(*vp)) ? (--cnt, getn(*vp++)) : MAILINTVL;
    if (intvl < 1)
	intvl = 1;
    if (chktim + intvl > t)
	return;
    for (; *vp; vp++) {
	if (stat(short2str(*vp), &stb) == -1)
	    continue;
	new = stb.st_mtime > time0.tv_sec;
	if (stb.st_size == 0 || stb.st_atime > stb.st_mtime ||
	    (stb.st_atime < chktim && stb.st_mtime < chktim) ||
	    (loginsh && !new))
	    continue;
	if (cnt == 1)
	    (void) fprintf(cshout, "You have %smail.\n", new ? "new " : "");
	else
	    (void) fprintf(cshout, "%s in %s.\n", new ? "New mail" : "Mail",
			   vis_str(*vp));
    }
    chktim = t;
}

/*
 * Extract a home directory from the password file
 * The argument points to a buffer where the name of the
 * user whose home directory is sought is currently.
 * We write the home directory of the user back there.
 */
int
gethdir(Char *home, int len)
{
    Char   *h;
    struct passwd *pw;

    /*
     * Is it us?
     */
    if (*home == '\0') {
	if ((h = value(STRhome)) != NULL) {
	    if (Strlcpy(home, h, len) >= len)
		return 1;
	    return 0;
	}
	else
	    return 1;
    }

    if ((pw = getpwnam(short2str(home))) != NULL) {
	if (Strlcpy(home, str2short(pw->pw_dir), len) >= len)
	    return 1;
	return 0;
    }
    else
	return 1;
}

/*
 * When didfds is set, we do I/O from 0, 1, 2 otherwise from 15, 16, 17
 * We also check if the shell has already changed the descriptor to point to
 * 0, 1, 2 when didfds is set.
 */
#define DESC(a) (*((int *) (a)) - (didfds && *((int *) a) >= FSHIN ? FSHIN : 0))

static int
readf(void *oreo, char *buf, int siz)
{
    return read(DESC(oreo), buf, siz);
}


static int
writef(void *oreo, const char *buf, int siz)
{
    return write(DESC(oreo), buf, siz);
}

static off_t
seekf(void *oreo, off_t off, int whence)
{
    return lseek(DESC(oreo), off, whence);
}


static int
closef(void *oreo)
{
    return close(DESC(oreo));
}


/*
 * Print the visible version of a string.
 */
int
vis_fputc(int ch, FILE *fp)
{
    char uenc[5];	/* 4 + NUL */

    if (ch & QUOTE)
	return fputc(ch & TRIM, fp);
    (void) vis(uenc, ch & TRIM, VIS_NOSLASH, 0);
    return fputs(uenc, fp);
}

/*
 * Move the initial descriptors to their eventual
 * resting places, closing all other units.
 */
void
initdesc(void)
{

    didfds = 0;			/* 0, 1, 2 aren't set up */
    (void) fcntl(SHIN = dcopy(0, FSHIN), F_SETFD, FD_CLOEXEC);
    (void) fcntl(SHOUT = dcopy(1, FSHOUT), F_SETFD, FD_CLOEXEC);
    (void) fcntl(SHERR = dcopy(2, FSHERR), F_SETFD, FD_CLOEXEC);
    (void) fcntl(OLDSTD = dcopy(SHIN, FOLDSTD), F_SETFD, FD_CLOEXEC);
    closem();
}


void
xexit(int i)
{
    untty();
    _exit(i);
}

static Char **
defaultpath(void)
{
    char   *ptr;
    Char  **blk, **blkp;
    struct stat stb;

    blkp = blk = xreallocarray(NULL, 10, sizeof(Char *));

#define DIRAPPEND(a)  \
	if (stat(ptr = a, &stb) == 0 && S_ISDIR(stb.st_mode)) \
		*blkp++ = SAVE(ptr)

    DIRAPPEND(_PATH_BIN);
    DIRAPPEND(_PATH_USRBIN);

#undef DIRAPPEND

    *blkp = NULL;
    return (blk);
}

void
printprompt(void)
{
    Char *cp;

    if (!whyles) {
	for (cp = value(STRprompt); *cp; cp++)
	    if (*cp == HIST)
		(void) fprintf(cshout, "%d", eventno + 1);
	    else if (*cp == '%' && *(cp + 1) == 'm') {
		char hostname[HOST_NAME_MAX + 1];
		char *p;

		gethostname(hostname, sizeof hostname);
		if ((p = strchr(hostname, '.')) != NULL)
		    *p = '\0';
		fprintf(cshout, "%s", hostname);
		cp++;
	    } else {
		if (*cp == '\\' && cp[1] == HIST)
		    cp++;
		(void) vis_fputc(*cp | QUOTE, cshout);
	    }
    }
    else
	/*
	 * Prompt for forward reading loop body content.
	 */
	(void) fprintf(cshout, "? ");
    (void) fflush(cshout);
}
