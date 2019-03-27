/* $Header: /p/tcsh/cvsroot/tcsh/sh.init.c,v 3.64 2013/02/11 13:51:16 christos Exp $ */
/*
 * sh.init.c: Function and signal tables
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

RCSID("$tcsh: sh.init.c,v 3.64 2013/02/11 13:51:16 christos Exp $")

#include "ed.h"
#include "tw.h"

/*
 * C shell
 */

#define	INF INT_MAX

const struct biltins bfunc[] = {
    { ":",		dozip,		0,	INF	},
    { "@",		dolet,		0,	INF	},
    { "alias",		doalias,	0,	INF	},
    { "alloc",		showall,	0,	1	},
#if defined(_CX_UX)
    { "att",		doatt,		0,	INF	},
#endif /* _CX_UX */
    { "bg",		dobg,		0,	INF	},
    { "bindkey",	dobindkey,	0,	8	},
    { "break",		dobreak,	0,	0	},
    { "breaksw",	doswbrk,	0,	0	},
#ifdef _OSD_POSIX
    { "bs2cmd",		dobs2cmd,	1,	INF	},
#endif /* OBSOLETE */
    { "builtins",	dobuiltins,	0,	0	},
#ifdef KAI
    { "bye",		goodbye,	0,	0	},
#endif /* KAI */
    { "case",		dozip,		0,	1	},
    { "cd",		dochngd,	0,	INF	},
    { "chdir",		dochngd,	0,	INF	},
    { "complete",	docomplete,	0,	INF	},
    { "continue",	docontin,	0,	0	},
    { "default",	dozip,		0,	0	},
    { "dirs",		dodirs,		0,	INF	},
#if defined(_CRAY) && !defined(_CRAYMPP)
    { "dmmode",		dodmmode,	0,	1	},
#endif /* _CRAY && !_CRAYMPP */
    { "echo",		doecho,		0,	INF	},
    { "echotc",		doechotc,	0,	INF	},
    { "else",		doelse,		0,	INF	},
    { "end",		doend,		0,	0	},
    { "endif",		dozip,		0,	0	},
    { "endsw",		dozip,		0,	0	},
    { "eval",		doeval,		0,	INF	},
    { "exec",		execash,	1,	INF	},
    { "exit",		doexit,		0,	INF	},
    { "fg",		dofg,		0,	INF	},
    { "filetest",	dofiletest,	2,	INF	},
    { "foreach",	doforeach,	3,	INF	},
#ifdef TCF
    { "getspath",	dogetspath,	0,	0	},
    { "getxvers",	dogetxvers,	0,	0	},
#endif /* TCF */
    { "glob",		doglob,		0,	INF	},
    { "goto",		dogoto,		1,	1	},
    { "hashstat",	hashstat,	0,	0	},
    { "history",	dohist,		0,	2	},
    { "hup",		dohup,		0,	INF	},
    { "if",		doif,		1,	INF	},
#ifdef apollo
    { "inlib", 		doinlib,	1,	INF	},
#endif /* apollo */
    { "jobs",		dojobs,		0,	1	},
    { "kill",		dokill,		1,	INF	},
#ifndef HAVENOLIMIT
    { "limit",		dolimit,	0,	3	},
#endif /* !HAVENOLIMIT */
#ifdef OBSOLETE
    { "linedit",	doecho,		0,	INF	},
#endif /* OBSOLETE */
#if !defined(HAVENOUTMP) && !defined(KAI)
    { "log",		dolog,		0,	0	},
#endif /* !HAVENOUTMP && !KAI */
    { "login",		dologin,	0,	1	},
    { "logout",		dologout,	0,	0	},
    { "ls-F",		dolist,		0,	INF	},
#ifdef TCF
    { "migrate",	domigrate,	1,	INF	},
#endif /* TCF */
#ifdef NEWGRP
    { "newgrp",		donewgrp,	0,	2	},
#endif /* NEWGRP */
    { "nice",		donice,		0,	INF	},
    { "nohup",		donohup,	0,	INF	},
    { "notify",		donotify,	0,	INF	},
    { "onintr",		doonintr,	0,	2	},
    { "popd",		dopopd,		0,	INF	},
    { "printenv",	doprintenv,	0,	1	},
    { "pushd",		dopushd,	0,	INF	},
    { "rehash",		dohash,		0,	3	},
    { "repeat",		dorepeat,	2,	INF	},
#ifdef apollo
    { "rootnode",	dorootnode,	1,	1	},
#endif /* apollo */
    { "sched",		dosched,	0,	INF	},
    { "set",		doset,		0,	INF	},
    { "setenv",		dosetenv,	0,	2	},
#ifdef MACH
    { "setpath",	dosetpath,	0,	INF	},
#endif	/* MACH */
#ifdef TCF
    { "setspath",	dosetspath,	1,	INF	},
#endif /* TCF */
    { "settc",		dosettc,	2,	2	},
    { "setty", 		dosetty,	0,      INF	},
#ifdef TCF
    { "setxvers",	dosetxvers,	0,	1	},
#endif /* TCF */
    { "shift",		shift,		0,	1	},
    { "source",		dosource,	1,	INF	},
    { "stop",		dostop,		1,	INF	},
    { "suspend",	dosuspend,	0,	0	},
    { "switch",		doswitch,	1,	INF	},
    { "telltc",		dotelltc,	0,	INF	},
#ifndef WINNT_NATIVE
    { "termname",	dotermname,	0,  	1       },
#endif
    { "time",		dotime,		0,	INF	},
#if defined(_CX_UX)
    { "ucb",		doucb,		0,	INF	},
#endif /* _CX_UX */
    { "umask",		doumask,	0,	1	},
    { "unalias",	unalias,	1,	INF	},
    { "uncomplete",	douncomplete,	1,	INF	},
    { "unhash",		dounhash,	0,	0	},
#if defined(masscomp) || defined(_CX_UX)
    { "universe",	douniverse,	0,	INF	},
#endif /* masscomp || _CX_UX */
#ifndef HAVENOLIMIT
    { "unlimit",	dounlimit,	0,	INF	},
#endif /* !HAVENOLIMIT */
    { "unset",		unset,		1,	INF	},
    { "unsetenv",	dounsetenv,	1,	INF	},
#ifdef apollo
    { "ver",		dover,		0,	INF	},
#endif /* apollo */
    { "wait",		dowait,		0,	0	},
#ifdef WARP
    { "warp",		dowarp,		0,	2	},
#endif /* WARP */
#if !defined(HAVENOUTMP) && defined(KAI)
    { "watchlog",	dolog,		0,	0	},
#endif /* !HAVENOUTMP && KAI */
    { "where",		dowhere,	1,	INF	},
    { "which",		dowhich,	1,	INF	},
    { "while",		dowhile,	1,	INF	}
};
int nbfunc = sizeof bfunc / sizeof *bfunc;

struct srch srchn[] = {
    { "@",		TC_LET		},
    { "break",		TC_BREAK	},
    { "breaksw",	TC_BRKSW	},
    { "case",		TC_CASE		},
    { "default", 	TC_DEFAULT	},
    { "else",		TC_ELSE		},
    { "end",		TC_END		},
    { "endif",		TC_ENDIF	},
    { "endsw",		TC_ENDSW	},
    { "exit",		TC_EXIT		},
    { "foreach", 	TC_FOREACH	},
    { "goto",		TC_GOTO		},
    { "if",		TC_IF		},
    { "label",		TC_LABEL	},
    { "set",		TC_SET		},
    { "switch",		TC_SWITCH	},
    { "while",		TC_WHILE	}
};
int nsrchn = sizeof srchn / sizeof *srchn;


/*
 * Note: For some machines, (hpux eg.)
 * NSIG = number of signals + 1...
 * so we define 33 or 65 (POSIX) signals for 
 * everybody
 */

/* We define NUMSIG to avoid changing NSIG or MAXSIG */
#if defined(POSIX) && (!defined(__CYGWIN__) || defined (__x86_64__))
# define NUMSIG 65
#else /* !POSIX */
# define NUMSIG 33
#endif /* POSIX */

int	nsig = NUMSIG - 1;	/* This should be the number of real signals */
				/* not counting signal 0 */
struct mesg mesg[NUMSIG];	/* Arrays start at [0] so we initialize from */
				/* 0 to 32 or 64, the max real signal number */

void
mesginit(void)
{

#ifdef NLS_CATALOGS
    int i;

    for (i = 0; i < NUMSIG; i++) {
        xfree((char *)(intptr_t)mesg[i].pname);
	mesg[i].pname = NULL;
    }
#endif /* NLS_CATALOGS */

#if defined(SIGNULL) || defined(DECOSF1)
# ifndef SIGNULL
#  define SIGNULL 0
# endif /* !SIGNULL */
    if (mesg[SIGNULL].pname == NULL) {
	mesg[SIGNULL].iname = "NULL";
	mesg[SIGNULL].pname = CSAVS(2, 1, "Null signal");
    }
#endif /* SIGNULL || DECOSF1 */

#ifdef SIGHUP
    if (mesg[SIGHUP].pname == NULL) {
	mesg[SIGHUP].iname = "HUP"; 
	mesg[SIGHUP].pname = CSAVS(2, 2, "Hangup");
    }
#endif /* SIGHUP */

#ifdef SIGINT
    if (mesg[SIGINT].pname == NULL) {
	mesg[SIGINT].iname = "INT";
	mesg[SIGINT].pname = CSAVS(2, 3, "Interrupt");
    }
#endif /* SIGINT */

#ifdef SIGQUIT
    if (mesg[SIGQUIT].pname == NULL) {
	mesg[SIGQUIT].iname = "QUIT";
	mesg[SIGQUIT].pname = CSAVS(2, 4, "Quit");
    }
#endif /* SIGQUIT */

#ifdef SIGILL
    if (mesg[SIGILL].pname == NULL) {
	mesg[SIGILL].iname = "ILL";
	mesg[SIGILL].pname = CSAVS(2, 5, "Illegal instruction");
    }
#endif /* SIGILL */

#ifdef SIGTRAP
    if (mesg[SIGTRAP].pname == NULL) {
	mesg[SIGTRAP].iname = "TRAP";
	mesg[SIGTRAP].pname = CSAVS(2, 6, "Trace/BPT trap");
    }
#endif /* SIGTRAP */

#ifdef SIGABRT
    if (mesg[SIGABRT].pname == NULL) {
	mesg[SIGABRT].iname = "ABRT";
	mesg[SIGABRT].pname = CSAVS(2, 7, "Abort");
    }
#endif /* SIGABRT */

#ifdef SIGIOT
    if (mesg[SIGIOT].pname == NULL) {
	mesg[SIGIOT].iname = "IOT";
	mesg[SIGIOT].pname = CSAVS(2, 8, "IOT trap");
    }
#endif /* SIGIOT */

#ifdef SIGDANGER
    /* aiws */
    if (mesg[SIGDANGER].pname == NULL) {
	mesg[SIGDANGER].iname = "DANGER";
	mesg[SIGDANGER].pname = CSAVS(2, 9, "System Crash Imminent");
    }
#endif /* SIGDANGER */

#ifdef SIGERR
    /* _CRAY */
    if (mesg[SIGERR].pname == NULL) {
	mesg[SIGERR].iname = "ERR";
	mesg[SIGERR].pname = CSAVS(2, 10, "Error exit");
    }
#endif /* SIGERR */

#ifdef SIGEMT
    if (mesg[SIGEMT].pname == NULL) {
	mesg[SIGEMT].iname = "EMT";
	mesg[SIGEMT].pname = CSAVS(2, 11, "EMT trap");
    }
#endif /* SIGEMT */

#ifdef SIGFPE
    if (mesg[SIGFPE].pname == NULL) {
	mesg[SIGFPE].iname = "FPE";
	mesg[SIGFPE].pname = CSAVS(2, 12, "Floating exception");
    }
#endif /* SIGFPE */

#ifdef SIGKILL
    if (mesg[SIGKILL].pname == NULL) {
	mesg[SIGKILL].iname = "KILL";
	mesg[SIGKILL].pname = CSAVS(2, 13, "Killed");
    }
#endif /* SIGKILL */

#ifdef SIGUSR1
    if (mesg[SIGUSR1].pname == NULL) {
	mesg[SIGUSR1].iname = "USR1";
	mesg[SIGUSR1].pname = CSAVS(2, 14, "User signal 1");
    }
#endif /* SIGUSR1 */

#ifdef SIGUSR2
    if (mesg[SIGUSR2].pname == NULL) {
	mesg[SIGUSR2].iname = "USR2";
	mesg[SIGUSR2].pname = CSAVS(2, 15, "User signal 2");
    }
#endif /* SIGUSR2 */

#ifdef SIGSEGV
    if (mesg[SIGSEGV].pname == NULL) {
	mesg[SIGSEGV].iname = "SEGV";
	mesg[SIGSEGV].pname = CSAVS(2, 16, "Segmentation fault");
    }
#endif /* SIGSEGV */

#ifdef SIGBUS
    if (mesg[SIGBUS].pname == NULL) {
	mesg[SIGBUS].iname = "BUS";
	mesg[SIGBUS].pname = CSAVS(2, 17, "Bus error");
    }
#endif /* SIGBUS */

#ifdef SIGPRE
    /* _CRAY || IBMAIX */
    if (mesg[SIGPRE].pname == NULL) {
	mesg[SIGPRE].iname = "PRE";
	mesg[SIGPRE].pname = CSAVS(2, 18, "Program range error");
    }
#endif /* SIGPRE */

#ifdef SIGORE
    /* _CRAY */
    if (mesg[SIGORE].pname == NULL) {
	mesg[SIGORE].iname = "ORE";
	mesg[SIGORE].pname = CSAVS(2, 19, "Operand range error");
    }
#endif /* SIGORE */

#ifdef SIGSYS
    if (mesg[SIGSYS].pname == NULL) {
	mesg[SIGSYS].iname = "SYS";
	mesg[SIGSYS].pname = CSAVS(2, 20, "Bad system call");
    }
#endif /* SIGSYS */

#ifdef SIGPIPE
    if (mesg[SIGPIPE].pname == NULL) {
	mesg[SIGPIPE].iname = "PIPE";
	mesg[SIGPIPE].pname = CSAVS(2, 21, "Broken pipe");
    }
#endif /* SIGPIPE */

#ifdef SIGALRM
    if (mesg[SIGALRM].pname == NULL) {
	mesg[SIGALRM].iname = "ALRM";
	mesg[SIGALRM].pname = CSAVS(2, 22, "Alarm clock");
    }
#endif /* SIGALRM */

#ifdef SIGTERM
    if (mesg[SIGTERM].pname == NULL) {
	mesg[SIGTERM].iname = "TERM";
	mesg[SIGTERM].pname = CSAVS(2, 23, "Terminated");
    }
#endif /* SIGTERM */

/* SIGCLD vs SIGCHLD */
#if !defined(SIGCHLD) || defined(SOLARIS2) || defined(apollo) || defined(__EMX__)
    /* If we don't define SIGCHLD, or our OS prefers SIGCLD to SIGCHLD, */
    /* check for SIGCLD */
# ifdef SIGCLD
    if (mesg[SIGCLD].pname == NULL) {
	mesg[SIGCLD].iname = "CLD";
#  ifdef BSDJOBS
	mesg[SIGCLD].pname = CSAVS(2, 24, "Child status change");
#  else /* !BSDJOBS */
	mesg[SIGCLD].pname = CSAVS(2, 25, "Death of child");
#  endif /* BSDJOBS */
    }
# endif /* SIGCLD */
#else /* !(!SIGCHLD || SOLARIS2 || apollo || __EMX__) */
    /* We probably define SIGCHLD */
# ifdef SIGCHLD
    if (mesg[SIGCHLD].pname == NULL) {
	mesg[SIGCHLD].iname = "CHLD";
#  ifdef BSDJOBS
	mesg[SIGCHLD].pname = CSAVS(2, 27, "Child stopped or exited");
#  else /* !BSDJOBS */
	mesg[SIGCHLD].pname = CSAVS(2, 28, "Child exited");
#  endif /* BSDJOBS */
    }
# endif /* SIGCHLD */
#endif /* !SIGCHLD || SOLARIS2 || apollo || __EMX__ */

#ifdef SIGAPOLLO
    /* apollo */
    if (mesg[SIGAPOLLO].pname == NULL) {
	mesg[SIGAPOLLO].iname = "APOLLO";
	mesg[SIGAPOLLO].pname = CSAVS(2, 26, "Apollo-specific fault");
    }
#endif /* SIGAPOLLO */

#ifdef SIGPWR
    if (mesg[SIGPWR].pname == NULL) {
	mesg[SIGPWR].iname = "PWR";
	mesg[SIGPWR].pname = CSAVS(2, 29, "Power failure");
    }
#endif /* SIGPWR */

#ifdef SIGLOST
    if (mesg[SIGLOST].pname == NULL) {
	mesg[SIGLOST].iname = "LOST";
	mesg[SIGLOST].pname = CSAVS(2, 30, "Resource Lost");
    }
#endif /* SIGLOST */

#ifdef SIGBREAK
    /* __EMX__ */
    if (mesg[SIGBREAK].pname == NULL) {
	mesg[SIGBREAK].iname = "BREAK";
	mesg[SIGBREAK].pname = CSAVS(2, 31, "Break (Ctrl-Break)");
    }
#endif /* SIGBREAK */

#ifdef SIGIO
# if !defined(SIGPOLL) || SIGPOLL != SIGIO
    if (mesg[SIGIO].pname == NULL) {
	mesg[SIGIO].iname = "IO";
#  ifdef cray
	mesg[SIGIO].pname = CSAVS(2, 32, "Input/output possible signal");
#  else /* !cray */
	mesg[SIGIO].pname = CSAVS(2, 33, "Asynchronous I/O (select)");
#  endif /* cray */
    }
# endif /* !SIGPOLL || SIGPOLL != SIGIO */
#endif /* SIGIO */

#ifdef SIGURG
    if (mesg[SIGURG].pname == NULL) {
	mesg[SIGURG].iname = "URG";
	mesg[SIGURG].pname = CSAVS(2, 34, "Urgent condition on I/O channel");
    }
#endif /* SIGURG */

#ifdef SIGMT
    /* cray */
    if (mesg[SIGMT].pname == NULL) {
	mesg[SIGMT].iname = "MT";
	mesg[SIGMT].pname = CSAVS(2, 35, "Multitasking wake-up");
    }
#endif /* SIGMT */

#ifdef SIGMTKILL
    /* cray */
    if (mesg[SIGMTKILL].pname == NULL) {
	mesg[SIGMTKILL].iname = "MTKILL";
	mesg[SIGMTKILL].pname = CSAVS(2, 36, "Multitasking kill");
    }
#endif /* SIGMTKILL */

#ifdef SIGBUFIO
    /* _CRAYCOM */
    if (mesg[SIGBUFIO].pname == NULL) {
	mesg[SIGBUFIO].iname = "BUFIO";
	mesg[SIGBUFIO].pname = CSAVS(2, 37,
				    "Fortran asynchronous I/O completion");
    }
#endif /* SIGBUFIO */

#ifdef SIGRECOVERY
    /* _CRAYCOM */
    if (mesg[SIGRECOVERY].pname == NULL) {
	mesg[SIGRECOVERY].iname = "RECOVERY";
	mesg[SIGRECOVERY].pname = CSAVS(2, 38, "Recovery");
    }
#endif /* SIGRECOVERY */

#ifdef SIGUME
    /* _CRAYCOM */
    if (mesg[SIGUME].pname == NULL) {
	mesg[SIGUME].iname = "UME";
	mesg[SIGUME].pname = CSAVS(2, 39, "Uncorrectable memory error");
    }
#endif /* SIGUME */

#ifdef SIGCPULIM
    /* _CRAYCOM */
    if (mesg[SIGCPULIM].pname == NULL) {
	mesg[SIGCPULIM].iname = "CPULIM";
	mesg[SIGCPULIM].pname = CSAVS(2, 40, "CPU time limit exceeded");
    }
#endif /* SIGCPULIM */

#ifdef SIGSHUTDN
    /* _CRAYCOM */
    if (mesg[SIGSHUTDN].pname == NULL) {
	mesg[SIGSHUTDN].iname = "SHUTDN";
	mesg[SIGSHUTDN].pname = CSAVS(2, 41, "System shutdown imminent");
    }
#endif /* SIGSHUTDN */

#ifdef SIGNOWAK
    /* _CRAYCOM */
    if (mesg[SIGNOWAK].pname == NULL) {
	mesg[SIGNOWAK].iname = "NOWAK";
	mesg[SIGNOWAK].pname = CSAVS(2, 42,
				    "Micro-tasking group-no wakeup flag set");
    }
#endif /* SIGNOWAK */

#ifdef SIGTHERR
    /* _CRAYCOM */
    if (mesg[SIGTHERR].pname == NULL) {
	mesg[SIGTHERR].iname = "THERR";
	mesg[SIGTHERR].pname = CSAVS(2, 43, 
			    "Thread error - (use cord -T for detailed info)");
    }
#endif /* SIGTHERR */

#ifdef SIGRPE
    /* cray */
    if (mesg[SIGRPE].pname == NULL) {
	mesg[SIGRPE].pname = CSAVS(2, 44, "CRAY Y-MP register parity error");
	mesg[SIGRPE].iname = "RPE";
    }
#endif /* SIGRPE */

#ifdef SIGINFO
    if (mesg[SIGINFO].pname == NULL) {
	mesg[SIGINFO].iname = "INFO";
	mesg[SIGINFO].pname = CSAVS(2, 45, "Information request");
    }
#endif /* SIGINFO */

#ifdef SIGSTOP
    if (mesg[SIGSTOP].pname == NULL) {
	mesg[SIGSTOP].iname = "STOP";
# ifdef SUSPENDED
	mesg[SIGSTOP].pname = CSAVS(2, 46, "Suspended (signal)");
# else /* !SUSPENDED */
	mesg[SIGSTOP].pname = CSAVS(2, 47, "Stopped (signal)");
# endif /* SUSPENDED */
    }
#endif /* SIGSTOP */

#ifdef SIGTSTP
    if (mesg[SIGTSTP].pname == NULL) {
	mesg[SIGTSTP].iname = "TSTP";
# ifdef SUSPENDED
	mesg[SIGTSTP].pname = CSAVS(2, 48, "Suspended");
# else /* !SUSPENDED */
	mesg[SIGTSTP].pname = CSAVS(2, 49, "Stopped");
# endif /* SUSPENDED */
    }
#endif /* SIGTSTP */

#ifdef SIGCONT
    if (mesg[SIGCONT].pname == NULL) {
	mesg[SIGCONT].iname = "CONT";
	mesg[SIGCONT].pname = CSAVS(2, 50, "Continued");
    }
#endif /* SIGCONT */

#ifdef SIGTTIN
    if (mesg[SIGTTIN].pname == NULL) {
	mesg[SIGTTIN].iname = "TTIN";
# ifdef SUSPENDED
	mesg[SIGTTIN].pname = CSAVS(2, 51, "Suspended (tty input)");
# else /* !SUSPENDED */
	mesg[SIGTTIN].pname = CSAVS(2, 52, "Stopped (tty input)");
# endif /* SUSPENDED */
    }
#endif /* SIGTTIN */

#ifdef SIGTTOU
    if (mesg[SIGTTOU].pname == NULL) {
	mesg[SIGTTOU].iname = "TTOU";
# ifdef SUSPENDED
	mesg[SIGTTOU].pname = CSAVS(2, 53, "Suspended (tty output)");
# else /* SUSPENDED */
	mesg[SIGTTOU].pname = CSAVS(2, 54, "Stopped (tty output)");
# endif /* SUSPENDED */
    }
#endif /* SIGTTOU */

#ifdef SIGWIND
    /* UNIXPC */
    if (mesg[SIGWIND].pname == NULL) {
	mesg[SIGWIND].iname = "WIND";
	mesg[SIGWIND].pname = CSAVS(2, 55, "Window status changed");
    }
#endif /* SIGWIND */

#ifdef SIGWINDOW
    if (mesg[SIGWINDOW].pname == NULL) {
	mesg[SIGWINDOW].iname = "WINDOW";
	mesg[SIGWINDOW].pname = CSAVS(2, 56, "Window size changed");
    }
#endif /* SIGWINDOW */

#ifdef SIGWINCH
    if (mesg[SIGWINCH].pname == NULL) {
	mesg[SIGWINCH].iname = "WINCH";
	mesg[SIGWINCH].pname = CSAVS(2, 56, "Window size changed");
    }
#endif /* SIGWINCH */

#ifdef SIGPHONE
    /* UNIXPC */
    if (mesg[SIGPHONE].pname == NULL) {
	mesg[SIGPHONE].iname = "PHONE";
	mesg[SIGPHONE].pname = CSAVS(2, 57, "Phone status changed");
    }
# endif /* SIGPHONE */

#ifdef SIGXCPU
    if (mesg[SIGXCPU].pname == NULL) {
	mesg[SIGXCPU].iname = "XCPU";
	mesg[SIGXCPU].pname = CSAVS(2, 58, "Cputime limit exceeded");
    }
#endif /* SIGXCPU */

#ifdef SIGXFSZ
    if (mesg[SIGXFSZ].pname == NULL) {
	mesg[SIGXFSZ].iname = "XFSZ";
	mesg[SIGXFSZ].pname = CSAVS(2, 59, "Filesize limit exceeded");
    }
#endif /* SIGXFSZ */

#ifdef SIGVTALRM
    if (mesg[SIGVTALRM].pname == NULL) {
	mesg[SIGVTALRM].iname = "VTALRM";
	mesg[SIGVTALRM].pname = CSAVS(2, 60, "Virtual time alarm");
    }
#endif /* SIGVTALRM */

#ifdef SIGPROF
    if (mesg[SIGPROF].pname == NULL) {
	mesg[SIGPROF].iname = "PROF";
	mesg[SIGPROF].pname = CSAVS(2, 61, "Profiling time alarm");
    }
#endif /* SIGPROF */

#ifdef SIGDIL
    /* hpux */
    if (mesg[SIGDIL].pname == NULL) {
	mesg[SIGDIL].iname = "DIL";
	mesg[SIGDIL].pname = CSAVS(2, 62, "DIL signal");
    }
#endif /* SIGDIL */

#ifdef SIGPOLL
    if (mesg[SIGPOLL].pname == NULL) {
	mesg[SIGPOLL].iname = "POLL";
	mesg[SIGPOLL].pname = CSAVS(2, 63, "Pollable event occured");
    }
#endif /* SIGPOLL */

#ifdef SIGWAITING
    /* solaris */
    if (mesg[SIGWAITING].pname == NULL) {
	mesg[SIGWAITING].iname = "WAITING";
	mesg[SIGWAITING].pname = CSAVS(2, 64, "Process's lwps are blocked");
    }
#endif /* SIGWAITING */

#ifdef SIGLWP
    /* solaris */
    if (mesg[SIGLWP].pname == NULL) {
	mesg[SIGLWP].iname = "LWP";
	mesg[SIGLWP].pname = CSAVS(2, 65, "Special LWP signal");
    }
#endif /* SIGLWP */

#ifdef SIGFREEZE
    /* solaris */
    if (mesg[SIGFREEZE].pname == NULL) {
	mesg[SIGFREEZE].iname = "FREEZE";
	mesg[SIGFREEZE].pname = CSAVS(2, 66, "Special CPR Signal");
    }
#endif /* SIGFREEZE */

#ifdef SIGTHAW
    /* solaris */
    if (mesg[SIGTHAW].pname == NULL) {
	mesg[SIGTHAW].iname = "THAW";
	mesg[SIGTHAW].pname = CSAVS(2, 67, "Special CPR Signal");
    }
#endif /* SIGTHAW */

#ifdef SIGCANCEL
    /* solaris */
    if (mesg[SIGCANCEL].pname == NULL) {
	mesg[SIGCANCEL].iname = "CANCEL";
	mesg[SIGCANCEL].pname = CSAVS(2, 109, 
	    "Thread cancellation signal used by libthread");
    }
#endif /* SIGCANCEL */

/*
 * Careful, some OS's (HP/UX 10.0) define these as -1
 */
#ifdef SIGRTMIN 
    /*
     * Cannot do this at compile time; Solaris2 uses _sysconf for these
     */
    if (SIGRTMIN > 0 && SIGRTMIN < NUMSIG) { 
	if (mesg[SIGRTMIN].pname == NULL) {
	    mesg[SIGRTMIN].iname = "RTMIN";
	    mesg[SIGRTMIN].pname = CSAVS(2, 68, "First Realtime Signal");
	}

	if (SIGRTMIN + 1 < SIGRTMAX && SIGRTMIN + 1 < NUMSIG &&
	    mesg[SIGRTMIN+1].pname == NULL) {
	    mesg[SIGRTMIN+1].iname = "RTMIN+1";
	    mesg[SIGRTMIN+1].pname = CSAVS(2, 69, "Second Realtime Signal");
	}

	if (SIGRTMIN + 2 < SIGRTMAX && SIGRTMIN + 2 < NUMSIG &&
	    mesg[SIGRTMIN+2].pname == NULL) {
	    mesg[SIGRTMIN+2].iname = "RTMIN+2";
	    mesg[SIGRTMIN+2].pname = CSAVS(2, 70, "Third Realtime Signal");
	}

	if (SIGRTMIN + 3 < SIGRTMAX && SIGRTMIN + 3 < NUMSIG &&
	    mesg[SIGRTMIN+3].pname == NULL) {
	    mesg[SIGRTMIN+3].iname = "RTMIN+3";
	    mesg[SIGRTMIN+3].pname = CSAVS(2, 71, "Fourth Realtime Signal");
	}
    }
#endif /* SIGRTMIN */

#ifdef SIGRTMAX
    /*
     * Cannot do this at compile time; Solaris2 uses _sysconf for these
     */
    if (SIGRTMAX > 0 && SIGRTMAX < NUMSIG) {
	if (SIGRTMAX - 3 > SIGRTMIN && mesg[SIGRTMAX-3].pname == NULL) {
	    mesg[SIGRTMAX-3].iname = "RTMAX-3";
	    mesg[SIGRTMAX-3].pname = CSAVS(2, 72,
					   "Fourth Last Realtime Signal");
	}

	if (SIGRTMAX - 2 > SIGRTMIN && mesg[SIGRTMAX-2].pname == NULL) {
	    mesg[SIGRTMAX-2].iname = "RTMAX-2";
	    mesg[SIGRTMAX-2].pname = CSAVS(2, 73,
					   "Third Last Realtime Signal");
	}

	if (SIGRTMAX - 1 > SIGRTMIN && mesg[SIGRTMAX-1].pname == NULL) {
	    mesg[SIGRTMAX-1].iname = "RTMAX-1";
	    mesg[SIGRTMAX-1].pname = CSAVS(2, 74,
					   "Second Last Realtime Signal");
	}

	if (SIGRTMAX > SIGRTMIN && mesg[SIGRTMAX].pname == NULL) {
	    mesg[SIGRTMAX].iname = "RTMAX";
	    mesg[SIGRTMAX].pname = CSAVS(2, 75,
					 "Last Realtime Signal");
	}
    }
#endif /* SIGRTMAX */


#ifdef SIGAIO
    /* aiws */
    if (mesg[SIGAIO].pname == NULL) {
	mesg[SIGAIO].iname = "AIO";
	mesg[SIGAIO].pname = CSAVS(2, 76, "LAN Asyncronous I/O");
    }
#endif /* SIGAIO */

#ifdef SIGPTY
    /* aiws */
    if (mesg[SIGPTY].pname == NULL) {
	mesg[SIGPTY].iname = "PTY";
	mesg[SIGPTY].pname = CSAVS(2, 77, "PTY read/write availability");
    }
#endif /* SIGPTY */

#ifdef SIGIOINT
    /* aiws */
    if (mesg[SIGIOINT].pname == NULL) {
	mesg[SIGIOINT].iname = "IOINT";
	mesg[SIGIOINT].pname = CSAVS(2, 78, "I/O intervention required");
    }
#endif /* SIGIOINT */

#ifdef SIGGRANT
    /* aiws */
    if (mesg[SIGGRANT].pname == NULL) {
	mesg[SIGGRANT].iname = "GRANT";
	mesg[SIGGRANT].pname = CSAVS(2, 79, "HFT monitor mode granted");
    }
#endif /* SIGGRANT */

#ifdef SIGRETRACT
    /* aiws */
    if (mesg[SIGRETRACT].pname == NULL) {
	mesg[SIGRETRACT].iname = "RETRACT";
	mesg[SIGRETRACT].pname = CSAVS(2, 80,
				  "HFT monitor mode should be relinguished");
    }
#endif /* SIGRETRACT */

#ifdef SIGSOUND
    /* aiws */
    if (mesg[SIGSOUND].pname == NULL) {
	mesg[SIGSOUND].iname = "SOUND";
	mesg[SIGSOUND].pname = CSAVS(2, 81, "HFT sound control has completed");
    }
#endif /* SIGSOUND */

#ifdef SIGSMSG
    /* aiws */
    if (mesg[SIGSMSG].pname == NULL) {
	mesg[SIGSMSG].iname = "SMSG";
	mesg[SIGSMSG].pname = CSAVS(2, 82, "Data in HFT ring buffer");
    }
#endif /* SIGMSG */

#ifdef SIGMIGRATE
    /* IBMAIX */
    if (mesg[SIGMIGRATE].pname == NULL) {
	mesg[SIGMIGRATE].iname = "MIGRATE";
	mesg[SIGMIGRATE].pname = CSAVS(2, 83, "Migrate process");
    }
#endif /* SIGMIGRATE */

#ifdef SIGSAK
    /* IBMAIX */
    if (mesg[SIGSAK].pname == NULL) {
	mesg[SIGSAK].iname = "SAK";
	mesg[SIGSAK].pname = CSAVS(2, 84, "Secure attention key");
    }
#endif /* SIGSAK */

#ifdef SIGRESCHED
    /* CX/UX */
    if (mesg[SIGRESCHED].pname == NULL) {
	mesg[SIGRESCHED].iname = "RESCHED";
	mesg[SIGRESCHED].pname = CSAVS(2, 85, "Reschedule");
    }
#endif /* SIGRESCHED */

#ifdef SIGDEBUG
    /* VMS_POSIX */
    if (mesg[SIGDEBUG].pname == NULL) {
	mesg[SIGDEBUG].iname = "DEBUG";
	mesg[SIGDEBUG].pname = CSAVS(2, 86, "Signaling SS$_DEBUG");
    }
#endif /* SIGDEBUG */

#ifdef SIGPRIO
    /* Lynx */
    if (mesg[SIGPRIO].pname == NULL) {
	mesg[SIGPRIO].iname = "PRIO";
	mesg[SIGPRIO].pname = CSAVS(2, 87, "Priority changed");
    }
#endif /* SIGPRIO */

#ifdef SIGDLK
    /* cray */
    if (mesg[SIGDLK].pname == NULL) {
	mesg[SIGDLK].iname = "DLK";
	mesg[SIGDLK].pname = CSAVS(2, 88, "True deadlock detected");
    }
#endif /* SIGDLK */

#ifdef SIGTINT
    /* masscomp */
    if (mesg[SIGTINT].pname == NULL) {
	mesg[SIGTINT].iname = "TINT";
	mesg[SIGTINT].pname = CSAVS(2, 89, "New input character");
    }
#endif /* SIGTINT */

#ifdef SIGSTKFLT
    if (mesg[SIGSTKFLT].pname == NULL) {
	mesg[SIGSTKFLT].iname = "STKFLT";
	mesg[SIGSTKFLT].pname = CSAVS(2, 90, "Stack limit exceeded");
    }
#endif /* SIGSTKFLT */

#ifdef SIGUNUSED
    if (mesg[SIGUNUSED].pname == NULL) {
	mesg[SIGUNUSED].iname = "UNUSED";
	mesg[SIGUNUSED].pname = CSAVS(2, 91, "Unused signal");
    }
#endif /* SIGUNUSED */

#ifdef SIGOVLY
    /* SX-4 */
    if (mesg[SIGOVLY].pname == NULL) {
	mesg[SIGOVLY].iname = "OVLY";
	mesg[SIGOVLY].pname = CSAVS(2, 92, "LM overlay");
    }
#endif /* SIGOVLY */

#ifdef SIGFRZ
    /* SX-4 */
    if (mesg[SIGFRZ].pname == NULL) {
	mesg[SIGFRZ].iname = "FRZ";
	mesg[SIGFRZ].pname = CSAVS(2, 93, "system freeze");
    }
#endif /* SIGFRZ */

#ifdef SIGDFRZ
    /* SX-4 */
    if (mesg[SIGDFRZ].pname == NULL) {
	mesg[SIGDFRZ].iname = "DFRZ";
	mesg[SIGDFRZ].pname = CSAVS(2, 94, "system defreeze");
    }
#endif /* SIGDFRZ */

#ifdef SIGDEAD
    /* SX-4 */
    if (mesg[SIGDEAD].pname == NULL) {
	mesg[SIGDEAD].iname = "DEAD";
	mesg[SIGDEAD].pname = CSAVS(2, 95, "dead lock");
    }
#endif /* SIGDEAD */

#ifdef SIGXMEM
    /* SX-4 */
    if (mesg[SIGXMEM].pname == NULL) {
	mesg[SIGXMEM].iname = "XMEM";
	mesg[SIGXMEM].pname = CSAVS(2, 96, "exceeded memory size limit");
    }
#endif /* SIGXMEM */

#ifdef SIGXDSZ
    /* SX-4 */
    if (mesg[SIGXDSZ].pname == NULL) {
	mesg[SIGXDSZ].iname = "XDSZ";
	mesg[SIGXDSZ].pname = CSAVS(2, 97, "exceeded data size limit");
    }
#endif /* SIGXDSZ */

#ifdef SIGMEM32
    /* SX-4 */
    if (mesg[SIGMEM32].pname == NULL) {
	mesg[SIGMEM32].iname = "MEM32";
	mesg[SIGMEM32].pname = CSAVS(2, 98, "exceeded memory size limit of 32KB");
    }
#endif /* SIGMEM32 */

#ifdef SIGNMEM
    /* SX-4 */
    if (mesg[SIGNMEM].pname == NULL) {
	mesg[SIGNMEM].iname = "NMEM";
	mesg[SIGNMEM].pname = CSAVS(2, 99, "exce error for no memory");
    }
#endif /* SIGNMEM */

#ifdef SIGCHKP
    /* SX-4 */
    if (mesg[SIGCHKP].pname == NULL) {
	mesg[SIGCHKP].iname = "CHKP";
	mesg[SIGCHKP].pname = CSAVS(2, 100, "check point start");
    }
#endif /* SIGCHKP */

#ifdef SIGKCHKP
#if 0
    /* SX-4 */
    if (mesg[SIGKCHKP].pname == NULL) {
	mesg[SIGKCHKP].iname = "KCHKP";
	mesg[SIGKCHKP].pname = CSAVS(2, 101, "check point start of kernel");
    }
#endif
#endif /* SIGKCHKP */

#ifdef SIGRSTA
    /* SX-4 */
    if (mesg[SIGRSTA].pname == NULL) {
	mesg[SIGRSTA].iname = "RSTA";
	mesg[SIGRSTA].pname = CSAVS(2, 102, "restart start");
    }
#endif /* SIGRSTA */

#ifdef SIGKRSTA
#if 0
    /* SX-4 */
    if (mesg[SIGKRSTA].pname == NULL) {
	mesg[SIGKRSTA].iname = "KRSTA";
	mesg[SIGKRSTA].pname = CSAVS(2, 103, "restart of kernel");
    }
#endif
#endif /* SIGKRSTA */

#ifdef SIGXXMU
    /* SX-4 */
    if (mesg[SIGXXMU].pname == NULL) {
	mesg[SIGXXMU].iname = "XXMU";
	mesg[SIGXXMU].pname = CSAVS(2, 104, "exeeded XMU size limit");
    }
#endif /* SIGXXMU */

#ifdef SIGXRLG0
    /* SX-4 */
    if (mesg[SIGXRLG0].pname == NULL) {
	mesg[SIGXRLG0].iname = "XRLG0";
	mesg[SIGXRLG0].pname = CSAVS(2, 105, "exeeded RLG0 limit");
    }
#endif /* SIGXRLG0 */

#ifdef SIGXRLG1
    /* SX-4 */
    if (mesg[SIGXRLG1].pname == NULL) {
	mesg[SIGXRLG1].iname = "XRLG1";
	mesg[SIGXRLG1].pname = CSAVS(2, 106, "exeeded RLG1 limit");
    }
#endif /* SIGXRLG1 */

#ifdef SIGXRLG2
    /* SX-4 */
    if (mesg[SIGXRLG2].pname == NULL) {
	mesg[SIGXRLG2].iname = "XRLG2";
	mesg[SIGXRLG2].pname = CSAVS(2, 107, "exeeded RLG2 limit");
    }
#endif /* SIGXRLG2 */

#ifdef SIGXRLG3
    /* SX-4 */
    if (mesg[SIGXRLG3].pname == NULL) {
	mesg[SIGXRLG3].iname = "XRLG3";
	mesg[SIGXRLG3].pname = CSAVS(2, 108, "exeeded RLG3 limit");
    }
#endif /* SIGXRLG3 */
}
