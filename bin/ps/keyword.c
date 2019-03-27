/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#if 0
#ifndef lint
static char sccsid[] = "@(#)keyword.c	8.5 (Berkeley) 4/2/94";
#endif /* not lint */
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <err.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxo/xo.h>

#include "ps.h"

static VAR *findvar(char *, int, char **header);
static int  vcmp(const void *, const void *);

/* Compute offset in common structures. */
#define	KOFF(x)	offsetof(struct kinfo_proc, x)
#define	ROFF(x)	offsetof(struct rusage, x)

#define	LWPFMT	"d"
#define	NLWPFMT	"d"
#define	UIDFMT	"u"
#define	PIDFMT	"d"

/* PLEASE KEEP THE TABLE BELOW SORTED ALPHABETICALLY!!! */
static VAR var[] = {
	{"%cpu", "%CPU", NULL, "percent-cpu", 0, pcpu, 0, CHAR, NULL, 0},
	{"%mem", "%MEM", NULL, "percent-memory", 0, pmem, 0, CHAR, NULL, 0},
	{"acflag", "ACFLG", NULL, "accounting-flag", 0, kvar, KOFF(ki_acflag),
	    USHORT, "x", 0},
	{"acflg", "", "acflag", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"args", "COMMAND", NULL, "arguments", COMM|LJUST|USER, arguments, 0,
	    CHAR, NULL, 0},
	{"blocked", "", "sigmask", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"caught", "", "sigcatch", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"class", "CLASS", NULL, "login-class", LJUST, loginclass, 0, CHAR,
	    NULL, 0},
	{"comm", "COMMAND", NULL, "command", LJUST, ucomm, 0, CHAR, NULL, 0},
	{"command", "COMMAND", NULL, "command", COMM|LJUST|USER, command, 0,
	    CHAR, NULL, 0},
	{"cow", "COW", NULL, "copy-on-write-faults", 0, kvar, KOFF(ki_cow),
	    UINT, "u", 0},
	{"cpu", "CPU", NULL, "cpu-usage", 0, kvar, KOFF(ki_estcpu), UINT, "d",
	    0},
	{"cputime", "", "time", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"dsiz", "DSIZ", NULL, "data-size", 0, kvar, KOFF(ki_dsize), PGTOK,
	    "ld", 0},
	{"egid", "", "gid", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"egroup", "", "group", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"emul", "EMUL", NULL, "emulation-envirnment", LJUST, emulname, 0,
	    CHAR, NULL, 0},
	{"etime", "ELAPSED", NULL, "elapsed-time", USER, elapsed, 0, CHAR,
	    NULL, 0},
	{"etimes", "ELAPSED", NULL, "elapsed-times", USER, elapseds, 0, CHAR,
	    NULL, 0},
	{"euid", "", "uid", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"f", "F", NULL, "flags", 0, kvar, KOFF(ki_flag), LONG, "lx", 0},
	{"f2", "F2", NULL, "flags2", 0, kvar, KOFF(ki_flag2), INT, "08x", 0},
	{"fib", "FIB", NULL, "fib", 0, kvar, KOFF(ki_fibnum), INT, "d", 0},
	{"flags", "", "f", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"flags2", "", "f2", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"gid", "GID", NULL, "gid", 0, kvar, KOFF(ki_groups), UINT, UIDFMT, 0},
	{"group", "GROUP", NULL, "group", LJUST, egroupname, 0, CHAR, NULL, 0},
	{"ignored", "", "sigignore", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"inblk", "INBLK", NULL, "read-blocks", USER, rvar, ROFF(ru_inblock),
	    LONG, "ld", 0},
	{"inblock", "", "inblk", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"jail", "JAIL", NULL, "jail-name", LJUST, jailname, 0, CHAR, NULL, 0},
	{"jid", "JID", NULL, "jail-id", 0, kvar, KOFF(ki_jid), INT, "d", 0},
	{"jobc", "JOBC", NULL, "job-control-count", 0, kvar, KOFF(ki_jobc),
	    SHORT, "d", 0},
	{"ktrace", "KTRACE", NULL, "ktrace", 0, kvar, KOFF(ki_traceflag), INT,
	    "x", 0},
	{"label", "LABEL", NULL, "label", LJUST, label, 0, CHAR, NULL, 0},
	{"lim", "LIM", NULL, "memory-limit", 0, maxrss, 0, CHAR, NULL, 0},
	{"lockname", "LOCK", NULL, "lock-name", LJUST, lockname, 0, CHAR, NULL,
	    0},
	{"login", "LOGIN", NULL, "login-name", LJUST, logname, 0, CHAR, NULL,
	    0},
	{"logname", "", "login", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"lstart", "STARTED", NULL, "start-time", LJUST|USER, lstarted, 0,
	    CHAR, NULL, 0},
	{"lwp", "LWP", NULL, "thread-id", 0, kvar, KOFF(ki_tid), UINT,
	    LWPFMT, 0},
	{"majflt", "MAJFLT", NULL, "major-faults", USER, rvar, ROFF(ru_majflt),
	    LONG, "ld", 0},
	{"minflt", "MINFLT", NULL, "minor-faults", USER, rvar, ROFF(ru_minflt),
	    LONG, "ld", 0},
	{"msgrcv", "MSGRCV", NULL, "received-messages", USER, rvar,
	    ROFF(ru_msgrcv), LONG, "ld", 0},
	{"msgsnd", "MSGSND", NULL, "sent-messages", USER, rvar,
	    ROFF(ru_msgsnd), LONG, "ld", 0},
	{"mwchan", "MWCHAN", NULL, "wait-channel", LJUST, mwchan, 0, CHAR,
	    NULL, 0},
	{"ni", "", "nice", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"nice", "NI", NULL, "nice", 0, kvar, KOFF(ki_nice), CHAR, "d", 0},
	{"nivcsw", "NIVCSW", NULL, "involuntary-context-switches", USER, rvar,
	    ROFF(ru_nivcsw), LONG, "ld", 0},
	{"nlwp", "NLWP", NULL, "threads", 0, kvar, KOFF(ki_numthreads), UINT,
	    NLWPFMT, 0},
	{"nsignals", "", "nsigs", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"nsigs", "NSIGS", NULL, "signals-taken", USER, rvar,
	    ROFF(ru_nsignals), LONG, "ld", 0},
	{"nswap", "NSWAP", NULL, "swaps", USER, rvar, ROFF(ru_nswap), LONG,
	    "ld", 0},
	{"nvcsw", "NVCSW", NULL, "voluntary-context-switches", USER, rvar,
	    ROFF(ru_nvcsw), LONG, "ld", 0},
	{"nwchan", "NWCHAN", NULL, "wait-channel-address", LJUST, nwchan, 0,
	    CHAR, NULL, 0},
	{"oublk", "OUBLK", NULL, "written-blocks", USER, rvar,
	    ROFF(ru_oublock), LONG, "ld", 0},
	{"oublock", "", "oublk", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"paddr", "PADDR", NULL, "process-address", 0, kvar, KOFF(ki_paddr),
	    KPTR, "lx", 0},
	{"pagein", "PAGEIN", NULL, "pageins", USER, pagein, 0, CHAR, NULL, 0},
	{"pcpu", "", "%cpu", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"pending", "", "sig", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"pgid", "PGID", NULL, "process-group", 0, kvar, KOFF(ki_pgid), UINT,
	    PIDFMT, 0},
	{"pid", "PID", NULL, "pid", 0, kvar, KOFF(ki_pid), UINT, PIDFMT, 0},
	{"pmem", "", "%mem", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"ppid", "PPID", NULL, "ppid", 0, kvar, KOFF(ki_ppid), UINT, PIDFMT, 0},
	{"pri", "PRI", NULL, "priority", 0, pri, 0, CHAR, NULL, 0},
	{"re", "RE", NULL, "residency-time", INF127, kvar, KOFF(ki_swtime),
	    UINT, "d", 0},
	{"rgid", "RGID", NULL, "real-gid", 0, kvar, KOFF(ki_rgid), UINT,
	    UIDFMT, 0},
	{"rgroup", "RGROUP", NULL, "real-group", LJUST, rgroupname, 0, CHAR,
	    NULL, 0},
	{"rss", "RSS", NULL, "rss", 0, kvar, KOFF(ki_rssize), PGTOK, "ld", 0},
	{"rtprio", "RTPRIO", NULL, "realtime-priority", 0, priorityr,
	    KOFF(ki_pri), CHAR, NULL, 0},
	{"ruid", "RUID", NULL, "real-uid", 0, kvar, KOFF(ki_ruid), UINT,
	    UIDFMT, 0},
	{"ruser", "RUSER", NULL, "real-user", LJUST, runame, 0, CHAR, NULL, 0},
	{"sid", "SID", NULL, "sid", 0, kvar, KOFF(ki_sid), UINT, PIDFMT, 0},
	{"sig", "PENDING", NULL, "signals-pending", 0, kvar, KOFF(ki_siglist),
	    INT, "x", 0},
	{"sigcatch", "CAUGHT", NULL, "signals-caught", 0, kvar,
	    KOFF(ki_sigcatch), UINT, "x", 0},
	{"sigignore", "IGNORED", NULL, "signals-ignored", 0, kvar,
	    KOFF(ki_sigignore), UINT, "x", 0},
	{"sigmask", "BLOCKED", NULL, "signal-mask", 0, kvar, KOFF(ki_sigmask),
	    UINT, "x", 0},
	{"sl", "SL", NULL, "sleep-time", INF127, kvar, KOFF(ki_slptime), UINT,
	    "d", 0},
	{"ssiz", "SSIZ", NULL, "stack-size", 0, kvar, KOFF(ki_ssize), PGTOK,
	    "ld", 0},
	{"start", "STARTED", NULL, "start-time", LJUST|USER, started, 0, CHAR,
	    NULL, 0},
	{"stat", "", "state", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"state", "STAT", NULL, "state", LJUST, state, 0, CHAR, NULL, 0},
	{"svgid", "SVGID", NULL, "saved-gid", 0, kvar, KOFF(ki_svgid), UINT,
	    UIDFMT, 0},
	{"svuid", "SVUID", NULL, "saved-uid", 0, kvar, KOFF(ki_svuid), UINT,
	    UIDFMT, 0},
	{"systime", "SYSTIME", NULL, "system-time", USER, systime, 0, CHAR,
	    NULL, 0},
	{"tdaddr", "TDADDR", NULL, "thread-address", 0, kvar, KOFF(ki_tdaddr),
	    KPTR, "lx", 0},
	{"tdev", "TDEV", NULL, "terminal-device", 0, tdev, 0, CHAR, NULL, 0},
	{"tdnam", "", "tdname", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"tdname", "TDNAME", NULL, "thread-name", LJUST, tdnam, 0, CHAR,
	    NULL, 0},
	{"tid", "", "lwp", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"time", "TIME", NULL, "cpu-time", USER, cputime, 0, CHAR, NULL, 0},
	{"tpgid", "TPGID", NULL, "terminal-process-gid", 0, kvar,
	    KOFF(ki_tpgid), UINT, PIDFMT, 0},
	{"tracer", "TRACER", NULL, "tracer", 0, kvar, KOFF(ki_tracer), UINT,
	    PIDFMT, 0},
	{"tsid", "TSID", NULL, "terminal-sid", 0, kvar, KOFF(ki_tsid), UINT,
	    PIDFMT, 0},
	{"tsiz", "TSIZ", NULL, "text-size", 0, kvar, KOFF(ki_tsize), PGTOK,
	    "ld", 0},
	{"tt", "TT ", NULL, "terminal-name", 0, tname, 0, CHAR, NULL, 0},
	{"tty", "TTY", NULL, "tty", LJUST, longtname, 0, CHAR, NULL, 0},
	{"ucomm", "UCOMM", NULL, "accounting-name", LJUST, ucomm, 0, CHAR,
	    NULL, 0},
	{"uid", "UID", NULL, "uid", 0, kvar, KOFF(ki_uid), UINT, UIDFMT, 0},
	{"upr", "UPR", NULL, "user-priority", 0, upr, 0, CHAR, NULL, 0},
	{"uprocp", "UPROCP", NULL, "process-address", 0, kvar, KOFF(ki_paddr),
	    KPTR, "lx", 0},
	{"user", "USER", NULL, "user", LJUST, uname, 0, CHAR, NULL, 0},
	{"usertime", "USERTIME", NULL, "user-time", USER, usertime, 0, CHAR,
	    NULL, 0},
	{"usrpri", "", "upr", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"vmaddr", "VMADDR", NULL, "vmspace-address", 0, kvar, KOFF(ki_vmspace),
	    KPTR, "lx", 0},
	{"vsize", "", "vsz", NULL, 0, NULL, 0, CHAR, NULL, 0},
	{"vsz", "VSZ", NULL, "virtual-size", 0, vsize, 0, CHAR, NULL, 0},
	{"wchan", "WCHAN", NULL, "wait-channel", LJUST, wchan, 0, CHAR, NULL,
	    0},
	{"xstat", "XSTAT", NULL, "exit-status", 0, kvar, KOFF(ki_xstat),
	    USHORT, "x", 0},
	{"", NULL, NULL, NULL, 0, NULL, 0, CHAR, NULL, 0},
};

void
showkey(void)
{
	VAR *v;
	int i;
	const char *p, *sep;

	i = 0;
	sep = "";
	xo_open_list("key");
	for (v = var; *(p = v->name); ++v) {
		int len = strlen(p);
		if (termwidth && (i += len + 1) > termwidth) {
			i = len;
			sep = "\n";
		}
		xo_emit("{P:/%s}{l:key/%s}", sep, p);
		sep = " ";
	}
	xo_emit("\n");
	xo_close_list("key");
	xo_finish();
}

void
parsefmt(const char *p, int user)
{
	char *tempstr, *tempstr1;

#define		FMTSEP	" \t,\n"
	tempstr1 = tempstr = strdup(p);
	while (tempstr && *tempstr) {
		char *cp, *hp;
		VAR *v;
		struct varent *vent;

		/*
		 * If an item contains an equals sign, it specifies a column
		 * header, may contain embedded separator characters and
		 * is always the last item.	
		 */
		if (tempstr[strcspn(tempstr, "="FMTSEP)] != '=')
			while ((cp = strsep(&tempstr, FMTSEP)) != NULL &&
			    *cp == '\0')
				/* void */;
		else {
			cp = tempstr;
			tempstr = NULL;
		}
		if (cp == NULL || !(v = findvar(cp, user, &hp)))
			continue;
		if (!user) {
			/*
			 * If the user is NOT adding this field manually,
			 * get on with our lives if this VAR is already
			 * represented in the list.
			 */
			vent = find_varentry(v);
			if (vent != NULL)
				continue;
		}
		if ((vent = malloc(sizeof(struct varent))) == NULL)
			errx(1, "malloc failed");
		vent->header = v->header;
		if (hp) {
			hp = strdup(hp);
			if (hp)
				vent->header = hp;
		}
		vent->var = malloc(sizeof(*vent->var));
		if (vent->var == NULL)
			errx(1, "malloc failed");
		memcpy(vent->var, v, sizeof(*vent->var));
		STAILQ_INSERT_TAIL(&varlist, vent, next_ve);
	}
	free(tempstr1);
	if (STAILQ_EMPTY(&varlist)) {
		warnx("no valid keywords; valid keywords:");
		showkey();
		exit(1);
	}
}

static VAR *
findvar(char *p, int user, char **header)
{
	size_t rflen;
	VAR *v, key;
	char *hp, *realfmt;

	hp = strchr(p, '=');
	if (hp)
		*hp++ = '\0';

	key.name = p;
	v = bsearch(&key, var, sizeof(var)/sizeof(VAR) - 1, sizeof(VAR), vcmp);

	if (v && v->alias) {
		/*
		 * If the user specified an alternate-header for this
		 * (aliased) format-name, then we need to copy that
		 * alternate-header when making the recursive call to
		 * process the alias.
		 */
		if (hp == NULL)
			parsefmt(v->alias, user);
		else {
			/*
			 * XXX - This processing will not be correct for
			 * any alias which expands into a list of format
			 * keywords.  Presently there are no aliases
			 * which do that.
			 */
			rflen = strlen(v->alias) + strlen(hp) + 2;
			realfmt = malloc(rflen);
			if (realfmt == NULL)
				errx(1, "malloc failed");
			snprintf(realfmt, rflen, "%s=%s", v->alias, hp);
			parsefmt(realfmt, user);
			free(realfmt);
		}
		return ((VAR *)NULL);
	}
	if (!v) {
		warnx("%s: keyword not found", p);
		eval = 1;
	}
	if (header)
		*header = hp;
	return (v);
}

static int
vcmp(const void *a, const void *b)
{
        return (strcmp(((const VAR *)a)->name, ((const VAR *)b)->name));
}
