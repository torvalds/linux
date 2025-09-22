/*	$OpenBSD: keyword.c,v 1.58 2025/07/02 13:24:48 deraadt Exp $	*/
/*	$NetBSD: keyword.c,v 1.12.6.1 1996/05/30 21:25:13 cgd Exp $	*/

/*-
 * Copyright (c) 1990, 1993, 1994
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
#include <sys/time.h>
#include <sys/resource.h>

#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps.h"

#include <sys/ucred.h>
#include <sys/sysctl.h>

int needheader;

static VAR *findvar(char *);
static int  vcmp(const void *, const void *);

#ifdef NOTINUSE
int	utime(), stime(), ixrss(), idrss(), isrss();
	{{"utime"}, "UTIME", USER, utime, 4},
	{{"stime"}, "STIME", USER, stime, 4},
	{{"ixrss"}, "IXRSS", USER, ixrss, 4},
	{{"idrss"}, "IDRSS", USER, idrss, 4},
	{{"isrss"}, "ISRSS", USER, isrss, 4},
#endif

/* Compute offset in common structures. */
#define	POFF(x)	offsetof(struct kinfo_proc, x)

#define	UIDFMT	"u"
#define	UIDLEN	5
#define	UID(n1, n2, fn, off) \
	{ n1, n2, NULL, 0, fn, UIDLEN, 0, off, UINT32, UIDFMT }
#define	GID(n1, n2, fn, off)	UID(n1, n2, fn, off)

#define	PIDFMT	"d"
#define	PIDLEN	5
#define	PID(n1, n2, fn, off) \
	{ n1, n2, NULL, 0, fn, PIDLEN, 0, off, INT32, PIDFMT }

#define	TIDFMT	"d"
#define TIDLEN	7
#define	TID(n1, n2, fn, off) \
	{ n1, n2, NULL, 0, fn, TIDLEN, 0, off, INT32, TIDFMT }

#define	USERLEN	8
#define	CWDLEN	40
#define	UCOMMLEN (sizeof(((struct kinfo_proc *)NULL)->p_comm) - 1)
#define	WCHANLEN (sizeof(((struct kinfo_proc *)NULL)->p_wmesg) - 1)

/* Bit types must match their respective entries in struct kinfo_proc */
/* Entries must be sorted in lexical ascending order! */
VAR var[] = {
	{"%cpu", "%CPU", NULL, 0, pcpu, 4},
	{"%mem", "%MEM", NULL, 0, pmem, 4},
	{"acflag", "ACFLG", NULL, 0, pvar, 3, 0, POFF(p_acflag), UINT32, "x"},
	{"acflg", "", "acflag"},
	{"args", "", "command"},
	{"blocked", "", "sigmask"},
	{"caught", "", "sigcatch"},
	{"comm", "COMMAND", "ucomm"},
	{"command", "COMMAND", NULL, COMM|LJUST|USER, command, 16},
	{"cpu", "CPU", NULL, 0, pvar, 3, 0, POFF(p_estcpu), UINT32, "d"},
	{"cpuid", "CPUID", NULL, 0, pvar, 8, 0, POFF(p_cpuid), UINT64, "lld"},
	{"cputime", "", "time"},
	{"cwd", "CWD", NULL, LJUST, curwd, CWDLEN},
	{"dsiz", "DSIZ", NULL, 0, dsize, 4},
	{"etime", "ELAPSED", NULL, USER, elapsed, 12},
	{"f", "F", NULL, 0, pvar, 7, 0, POFF(p_flag), INT32, "x"},
	{"flags", "", "f"},
	GID("gid", "GID", pvar, POFF(p_gid)),
	{"group", "GROUP", NULL, LJUST, gname, USERLEN},
	{"ignored", "", "sigignore"},
	{"inblk", "INBLK", NULL, USER, pvar, 4, 0, POFF(p_uru_inblock), UINT64, "lld"},
	{"inblock", "", "inblk"},
	{"jobc", "JOBC", NULL, 0, pvar, 4, 0, POFF(p_jobc), INT16, "d"},
	{"ktrace", "KTRACE", NULL, 0, pvar, 8, 0, POFF(p_traceflag), INT32, "x"},
	/* XXX */
	{"ktracep", "KTRACEP", NULL, 0, pvar, PTRWIDTH, 0, POFF(p_tracep), UINT64, "llx"},
	{"lim", "LIM", NULL, 0, maxrss, 5},
	{"login", "LOGIN", NULL, LJUST, logname, LOGIN_NAME_MAX},
	{"logname", "", "login"},
	{"lstart", "STARTED", NULL, LJUST|USER, lstarted, 28},
	{"majflt", "MAJFLT", NULL, USER, pvar, 4, 0, POFF(p_uru_majflt), UINT64, "lld"},
	{"maxrss", "MAXRSS", NULL, USER, pvar, 4, 0, POFF(p_uru_maxrss), UINT64, "lld"},
	{"minflt", "MINFLT", NULL, USER, pvar, 4, 0, POFF(p_uru_minflt), UINT64, "lld"},
	{"msgrcv", "MSGRCV", NULL, USER, pvar, 4, 0, POFF(p_uru_msgrcv), UINT64, "lld"},
	{"msgsnd", "MSGSND", NULL, USER, pvar, 4, 0, POFF(p_uru_msgsnd), UINT64, "lld"},
	{"ni", "", "nice"},
	{"nice", "NI", NULL, 0, pnice, 3},
	{"nivcsw", "NIVCSW", NULL, USER, pvar, 5, 0, POFF(p_uru_nivcsw), UINT64, "lld"},
	{"nsignals", "", "nsigs"},
	{"nsigs", "NSIGS", NULL, USER, pvar, 4, 0, POFF(p_uru_nsignals), UINT64, "lld"},
	{"nswap", "NSWAP", NULL, USER, pvar, 4, 0, POFF(p_uru_nswap), UINT64, "lld"},
	{"nvcsw", "NVCSW", NULL, USER, pvar, 5, 0, POFF(p_uru_nvcsw), UINT64, "lld"},
	/* XXX */
	{"nwchan", "WCHAN", NULL, 0, pvar, PTRWIDTH, 0, POFF(p_wchan), UINT64, "llx"},
	{"oublk", "OUBLK", NULL, USER, pvar, 4, 0, POFF(p_uru_oublock), UINT64, "lld"},
	{"oublock", "", "oublk"},
	/* XXX */
	{"p_ru", "P_RU", NULL, 0, pvar, PTRWIDTH, 0, POFF(p_ru), UINT64, "llx"},
	/* XXX */
	{"paddr", "PADDR", NULL, 0, pvar, PTRWIDTH, 0, POFF(p_paddr), UINT64, "llx"},
	{"pagein", "PAGEIN", NULL, USER, pagein, 6},
	{"pcpu", "", "%cpu"},
	{"pending", "", "sig"},
	PID("pgid", "PGID", pvar, POFF(p__pgid)),
	PID("pid", "PID", pvar, POFF(p_pid)),
	{"pledge", "PLEDGE", NULL, LJUST, printpledge, 64},
	{"pmem", "", "%mem"},
	PID("ppid", "PPID", pvar, POFF(p_ppid)),
	{"pri", "PRI", NULL, 0, pri, 3},
	{"procflags", "PROCF", NULL, 0, pvar, 7, 0, POFF(p_psflags), INT32, "x"},
	{"re", "RE", NULL, INF127, pvar, 3, 0, POFF(p_swtime), UINT32, "u"},
	GID("rgid", "RGID", pvar, POFF(p_rgid)),
	/* XXX */
	{"rgroup", "RGROUP", NULL, LJUST, rgname, USERLEN},
	{"rlink", "RLINK", NULL, 0, pvar, 8, 0, POFF(p_back), UINT64, "llx"},
	{"rss", "RSS", NULL, 0, p_rssize, 5},
	{"rtable", "RTABLE", NULL, 0, pvar, 0, 0, POFF(p_rtableid), INT32, "d"},
	UID("ruid", "RUID", pvar, POFF(p_ruid)),
	{"ruser", "RUSER", NULL, LJUST, runame, USERLEN},
	PID("sess", "SESS", pvar, POFF(p_sid)),
	{"sig", "PENDING", NULL, 0, pvar, 8, 0, POFF(p_siglist), INT32, "x"},
	{"sigcatch", "CAUGHT", NULL, 0, pvar, 8, 0, POFF(p_sigcatch), UINT32, "x"},
	{"sigignore", "IGNORED",
		NULL, 0, pvar, 8, 0, POFF(p_sigignore), UINT32, "x"},
	{"sigmask", "BLOCKED", NULL, 0, pvar, 8, 0, POFF(p_sigmask), UINT32, "x"},
	{"sl", "SL", NULL, INF127, pvar, 3, 0, POFF(p_slptime), UINT32, "u"},
	{"ssiz", "SSIZ", NULL, 0, ssize, 4},
	{"start", "STARTED", NULL, LJUST|USER, started, 8},
	{"stat", "", "state"},
	{"state", "STAT", NULL, LJUST, printstate, 6},
	{"supgid", "SUPGID", NULL, LJUST, supgid, 64},
	{"supgrp", "SUPGRP", NULL, LJUST, supgrp, 64},
	GID("svgid", "SVGID", pvar, POFF(p_svgid)),
	UID("svuid", "SVUID", pvar, POFF(p_svuid)),
	{"tdev", "TDEV", NULL, 0, tdev, 4},
	TID("tid", "TID", pvar, POFF(p_tid)),
	{"time", "TIME", NULL, USER, cputime, 9},
	PID("tpgid", "TPGID", pvar, POFF(p_tpgid)),
	{"tsess", "TSESS", NULL, 0, pvar, PTRWIDTH, 0, POFF(p_tsess), UINT64, "llx"},
	{"tsiz", "TSIZ", NULL, 0, tsize, 4},
	{"tt", "TT", NULL, LJUST, tname, 3},
	{"tty", "TTY", NULL, LJUST, longtname, 8},
	{"ucomm", "UCOMM", NULL, LJUST, ucomm, UCOMMLEN},
	UID("uid", "UID", pvar, POFF(p_uid)),
	{"upr", "UPR", NULL, 0, pvar, 3, 0, POFF(p_usrpri), UINT8, "d"},
	{"user", "USER", NULL, LJUST, euname, USERLEN},
	{"usrpri", "", "upr"},
	{"vsize", "", "vsz"},
	{"vsz", "VSZ", NULL, 0, vsize, 5},
	{"wchan", "WCHAN", NULL, LJUST, wchan, WCHANLEN},
	{"xstat", "XSTAT", NULL, 0, pvar, 4, 0, POFF(p_xstat), UINT16, "x"},
	{""},
};

void
showkey(void)
{
	VAR *v;
	int i;
	char *p, *sep;

	i = 0;
	sep = "";
	for (v = var; *(p = v->name); ++v) {
		int len = strlen(p);
		if (termwidth && (i += len + 1) > termwidth) {
			i = len;
			sep = "\n";
		}
		(void) printf("%s%s", sep, p);
		sep = " ";
	}
	(void) printf("\n");
}

int
parsefmt(char *p)
{
	static struct varent *vtail;
	int rval = 0;

#define	FMTSEP	" \t,\n"
	while (p && *p) {
		char *cp;
		VAR *v;
		struct varent *vent;

		while ((cp = strsep(&p, FMTSEP)) != NULL && *cp == '\0')
			/* void */;
		if (!cp)
			break;
		v = findvar(cp);
		if (v == NULL) {
			rval = 1;
			continue;
		}
		if (v->parsed == 1)
			continue;
		v->parsed = 1;
		if ((vent = malloc(sizeof(struct varent))) == NULL)
			err(1, NULL);
		vent->var = v;
		vent->next = NULL;
		if (vhead == NULL)
			vhead = vtail = vent;
		else {
			vtail->next = vent;
			vtail = vent;
		}
		needheader |= v->header[0] != '\0';
	}
	if (!vhead)
		errx(1, "no valid keywords");
	return rval;
}

static VAR *
findvar(char *p)
{
	VAR *v, key;
	char *hp;

	key.name = p;

	hp = strchr(p, '=');
	if (hp)
		*hp++ = '\0';

aliased:
	key.name = p;
	v = bsearch(&key, var, sizeof(var)/sizeof(VAR) - 1, sizeof(VAR), vcmp);

	if (v && v->alias) {
		p = v->alias;
		if (hp == NULL && v->header[0] != '\0')
			hp = v->header;
		goto aliased;
	}
	if (!v) {
		warnx("%s: keyword not found", p);
		return (NULL);
	}
	if (hp)
		v->header = hp;
	return (v);
}

static int
vcmp(const void *a, const void *b)
{
	return (strcmp(((VAR *)a)->name, ((VAR *)b)->name));
}
