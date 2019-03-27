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
static char sccsid[] = "@(#)print.c	8.6 (Berkeley) 4/16/94";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/stat.h>

#include <sys/mac.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>

#include <err.h>
#include <grp.h>
#include <jail.h>
#include <langinfo.h>
#include <locale.h>
#include <math.h>
#include <nlist.h>
#include <pwd.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vis.h>
#include <libxo/xo.h>

#include "ps.h"

#define	COMMAND_WIDTH	16
#define	ARGUMENTS_WIDTH	16

#define	ps_pgtok(a)	(((a) * getpagesize()) / 1024)

void
printheader(void)
{
	VAR *v;
	struct varent *vent;

	STAILQ_FOREACH(vent, &varlist, next_ve)
		if (*vent->header != '\0')
			break;
	if (!vent)
		return;

	STAILQ_FOREACH(vent, &varlist, next_ve) {
		v = vent->var;
		if (v->flag & LJUST) {
			if (STAILQ_NEXT(vent, next_ve) == NULL)	/* last one */
				xo_emit("{T:/%s}", vent->header);
			else
				xo_emit("{T:/%-*s}", v->width, vent->header);
		} else
			xo_emit("{T:/%*s}", v->width, vent->header);
		if (STAILQ_NEXT(vent, next_ve) != NULL)
			xo_emit("{P: }");
	}
	xo_emit("\n");
}

char *
arguments(KINFO *k, VARENT *ve)
{
	char *vis_args;

	if ((vis_args = malloc(strlen(k->ki_args) * 4 + 1)) == NULL)
		xo_errx(1, "malloc failed");
	strvis(vis_args, k->ki_args, VIS_TAB | VIS_NL | VIS_NOSLASH);

	if (STAILQ_NEXT(ve, next_ve) != NULL && strlen(vis_args) > ARGUMENTS_WIDTH)
		vis_args[ARGUMENTS_WIDTH] = '\0';

	return (vis_args);
}

char *
command(KINFO *k, VARENT *ve)
{
	char *vis_args, *vis_env, *str;

	if (cflag) {
		/* If it is the last field, then don't pad */
		if (STAILQ_NEXT(ve, next_ve) == NULL) {
			asprintf(&str, "%s%s%s%s%s",
			    k->ki_d.prefix ? k->ki_d.prefix : "",
			    k->ki_p->ki_comm,
			    (showthreads && k->ki_p->ki_numthreads > 1) ? "/" : "",
			    (showthreads && k->ki_p->ki_numthreads > 1) ? k->ki_p->ki_tdname : "",
			    (showthreads && k->ki_p->ki_numthreads > 1) ? k->ki_p->ki_moretdname : "");
		} else
			str = strdup(k->ki_p->ki_comm);

		return (str);
	}
	if ((vis_args = malloc(strlen(k->ki_args) * 4 + 1)) == NULL)
		xo_errx(1, "malloc failed");
	strvis(vis_args, k->ki_args, VIS_TAB | VIS_NL | VIS_NOSLASH);

	if (STAILQ_NEXT(ve, next_ve) == NULL) {
		/* last field */

		if (k->ki_env) {
			if ((vis_env = malloc(strlen(k->ki_env) * 4 + 1))
			    == NULL)
				xo_errx(1, "malloc failed");
			strvis(vis_env, k->ki_env,
			    VIS_TAB | VIS_NL | VIS_NOSLASH);
		} else
			vis_env = NULL;

		asprintf(&str, "%s%s%s%s",
		    k->ki_d.prefix ? k->ki_d.prefix : "",
		    vis_env ? vis_env : "",
		    vis_env ? " " : "",
		    vis_args);

		if (vis_env != NULL)
			free(vis_env);
		free(vis_args);
	} else {
		/* ki_d.prefix & ki_env aren't shown for interim fields */
		str = vis_args;

		if (strlen(str) > COMMAND_WIDTH)
			str[COMMAND_WIDTH] = '\0';
	}

	return (str);
}

char *
ucomm(KINFO *k, VARENT *ve)
{
	char *str;

	if (STAILQ_NEXT(ve, next_ve) == NULL) {	/* last field, don't pad */
		asprintf(&str, "%s%s%s%s%s",
		    k->ki_d.prefix ? k->ki_d.prefix : "",
		    k->ki_p->ki_comm,
		    (showthreads && k->ki_p->ki_numthreads > 1) ? "/" : "",
		    (showthreads && k->ki_p->ki_numthreads > 1) ? k->ki_p->ki_tdname : "",
		    (showthreads && k->ki_p->ki_numthreads > 1) ? k->ki_p->ki_moretdname : "");
	} else {
		if (showthreads && k->ki_p->ki_numthreads > 1)
			asprintf(&str, "%s/%s%s", k->ki_p->ki_comm,
			    k->ki_p->ki_tdname, k->ki_p->ki_moretdname);
		else
			str = strdup(k->ki_p->ki_comm);
	}
	return (str);
}

char *
tdnam(KINFO *k, VARENT *ve __unused)
{
	char *str;

	if (showthreads && k->ki_p->ki_numthreads > 1)
		asprintf(&str, "%s%s", k->ki_p->ki_tdname,
		    k->ki_p->ki_moretdname);
	else
		str = strdup("      ");

	return (str);
}

char *
logname(KINFO *k, VARENT *ve __unused)
{

	if (*k->ki_p->ki_login == '\0')
		return (NULL);
	return (strdup(k->ki_p->ki_login));
}

char *
state(KINFO *k, VARENT *ve __unused)
{
	long flag, tdflags;
	char *cp, *buf;

	buf = malloc(16);
	if (buf == NULL)
		xo_errx(1, "malloc failed");

	flag = k->ki_p->ki_flag;
	tdflags = k->ki_p->ki_tdflags;	/* XXXKSE */
	cp = buf;

	switch (k->ki_p->ki_stat) {

	case SSTOP:
		*cp = 'T';
		break;

	case SSLEEP:
		if (tdflags & TDF_SINTR)	/* interruptable (long) */
			*cp = k->ki_p->ki_slptime >= MAXSLP ? 'I' : 'S';
		else
			*cp = 'D';
		break;

	case SRUN:
	case SIDL:
		*cp = 'R';
		break;

	case SWAIT:
		*cp = 'W';
		break;

	case SLOCK:
		*cp = 'L';
		break;

	case SZOMB:
		*cp = 'Z';
		break;

	default:
		*cp = '?';
	}
	cp++;
	if (!(flag & P_INMEM))
		*cp++ = 'W';
	if (k->ki_p->ki_nice < NZERO || k->ki_p->ki_pri.pri_class == PRI_REALTIME)
		*cp++ = '<';
	else if (k->ki_p->ki_nice > NZERO || k->ki_p->ki_pri.pri_class == PRI_IDLE)
		*cp++ = 'N';
	if (flag & P_TRACED)
		*cp++ = 'X';
	if (flag & P_WEXIT && k->ki_p->ki_stat != SZOMB)
		*cp++ = 'E';
	if (flag & P_PPWAIT)
		*cp++ = 'V';
	if ((flag & P_SYSTEM) || k->ki_p->ki_lock > 0)
		*cp++ = 'L';
	if ((k->ki_p->ki_cr_flags & CRED_FLAG_CAPMODE) != 0)
		*cp++ = 'C';
	if (k->ki_p->ki_kiflag & KI_SLEADER)
		*cp++ = 's';
	if ((flag & P_CONTROLT) && k->ki_p->ki_pgid == k->ki_p->ki_tpgid)
		*cp++ = '+';
	if (flag & P_JAILED)
		*cp++ = 'J';
	*cp = '\0';
	return (buf);
}

#define	scalepri(x)	((x) - PZERO)

char *
pri(KINFO *k, VARENT *ve __unused)
{
	char *str;

	asprintf(&str, "%d", scalepri(k->ki_p->ki_pri.pri_level));
	return (str);
}

char *
upr(KINFO *k, VARENT *ve __unused)
{
	char *str;

	asprintf(&str, "%d", scalepri(k->ki_p->ki_pri.pri_user));
	return (str);
}
#undef scalepri

char *
uname(KINFO *k, VARENT *ve __unused)
{

	return (strdup(user_from_uid(k->ki_p->ki_uid, 0)));
}

char *
egroupname(KINFO *k, VARENT *ve __unused)
{

	return (strdup(group_from_gid(k->ki_p->ki_groups[0], 0)));
}

char *
rgroupname(KINFO *k, VARENT *ve __unused)
{

	return (strdup(group_from_gid(k->ki_p->ki_rgid, 0)));
}

char *
runame(KINFO *k, VARENT *ve __unused)
{

	return (strdup(user_from_uid(k->ki_p->ki_ruid, 0)));
}

char *
tdev(KINFO *k, VARENT *ve __unused)
{
	dev_t dev;
	char *str;

	dev = k->ki_p->ki_tdev;
	if (dev == NODEV)
		str = strdup("-");
	else
		asprintf(&str, "%#jx", (uintmax_t)dev);

	return (str);
}

char *
tname(KINFO *k, VARENT *ve __unused)
{
	dev_t dev;
	char *ttname, *str;

	dev = k->ki_p->ki_tdev;
	if (dev == NODEV || (ttname = devname(dev, S_IFCHR)) == NULL)
		str = strdup("- ");
	else {
		if (strncmp(ttname, "tty", 3) == 0 ||
		    strncmp(ttname, "cua", 3) == 0)
			ttname += 3;
		if (strncmp(ttname, "pts/", 4) == 0)
			ttname += 4;
		asprintf(&str, "%s%c", ttname,
		    k->ki_p->ki_kiflag & KI_CTTY ? ' ' : '-');
	}

	return (str);
}

char *
longtname(KINFO *k, VARENT *ve __unused)
{
	dev_t dev;
	const char *ttname;

	dev = k->ki_p->ki_tdev;
	if (dev == NODEV || (ttname = devname(dev, S_IFCHR)) == NULL)
		ttname = "-";

	return (strdup(ttname));
}

char *
started(KINFO *k, VARENT *ve __unused)
{
	time_t then;
	struct tm *tp;
	size_t buflen = 100;
	char *buf;

	if (!k->ki_valid)
		return (NULL);

	buf = malloc(buflen);
	if (buf == NULL)
		xo_errx(1, "malloc failed");

	then = k->ki_p->ki_start.tv_sec;
	tp = localtime(&then);
	if (now - k->ki_p->ki_start.tv_sec < 24 * 3600) {
		(void)strftime(buf, buflen, "%H:%M  ", tp);
	} else if (now - k->ki_p->ki_start.tv_sec < 7 * 86400) {
		(void)strftime(buf, buflen, "%a%H  ", tp);
	} else
		(void)strftime(buf, buflen, "%e%b%y", tp);
	return (buf);
}

char *
lstarted(KINFO *k, VARENT *ve __unused)
{
	time_t then;
	char *buf;
	size_t buflen = 100;

	if (!k->ki_valid)
		return (NULL);

	buf = malloc(buflen);
	if (buf == NULL)
		xo_errx(1, "malloc failed");

	then = k->ki_p->ki_start.tv_sec;
	(void)strftime(buf, buflen, "%c", localtime(&then));
	return (buf);
}

char *
lockname(KINFO *k, VARENT *ve __unused)
{
	char *str;

	if (k->ki_p->ki_kiflag & KI_LOCKBLOCK) {
		if (k->ki_p->ki_lockname[0] != 0)
			str = strdup(k->ki_p->ki_lockname);
		else
			str = strdup("???");
	} else
		str = NULL;

	return (str);
}

char *
wchan(KINFO *k, VARENT *ve __unused)
{
	char *str;

	if (k->ki_p->ki_wchan) {
		if (k->ki_p->ki_wmesg[0] != 0)
			str = strdup(k->ki_p->ki_wmesg);
		else
			asprintf(&str, "%lx", (long)k->ki_p->ki_wchan);
	} else
		str = NULL;

	return (str);
}

char *
nwchan(KINFO *k, VARENT *ve __unused)
{
	char *str;

	if (k->ki_p->ki_wchan)
		asprintf(&str, "%0lx", (long)k->ki_p->ki_wchan);
	else
		str = NULL;

	return (str);
}

char *
mwchan(KINFO *k, VARENT *ve __unused)
{
	char *str;

	if (k->ki_p->ki_wchan) {
		if (k->ki_p->ki_wmesg[0] != 0)
			str = strdup(k->ki_p->ki_wmesg);
		else
                        asprintf(&str, "%lx", (long)k->ki_p->ki_wchan);
	} else if (k->ki_p->ki_kiflag & KI_LOCKBLOCK) {
		if (k->ki_p->ki_lockname[0]) {
			str = strdup(k->ki_p->ki_lockname);
		} else
			str = strdup("???");
	} else
		str = NULL;

	return (str);
}

char *
vsize(KINFO *k, VARENT *ve __unused)
{
	char *str;

	asprintf(&str, "%lu", (u_long)(k->ki_p->ki_size / 1024));
	return (str);
}

static char *
printtime(KINFO *k, VARENT *ve __unused, long secs, long psecs)
/* psecs is "parts" of a second. first micro, then centi */
{
	static char decimal_point;
	char *str;

	if (decimal_point == '\0')
		decimal_point = localeconv()->decimal_point[0];
	if (!k->ki_valid) {
		secs = 0;
		psecs = 0;
	} else {
		/* round and scale to 100's */
		psecs = (psecs + 5000) / 10000;
		secs += psecs / 100;
		psecs = psecs % 100;
	}
	asprintf(&str, "%ld:%02ld%c%02ld",
	    secs / 60, secs % 60, decimal_point, psecs);
	return (str);
}

char *
cputime(KINFO *k, VARENT *ve)
{
	long secs, psecs;

	/*
	 * This counts time spent handling interrupts.  We could
	 * fix this, but it is not 100% trivial (and interrupt
	 * time fractions only work on the sparc anyway).	XXX
	 */
	secs = k->ki_p->ki_runtime / 1000000;
	psecs = k->ki_p->ki_runtime % 1000000;
	if (sumrusage) {
		secs += k->ki_p->ki_childtime.tv_sec;
		psecs += k->ki_p->ki_childtime.tv_usec;
	}
	return (printtime(k, ve, secs, psecs));
}

char *
systime(KINFO *k, VARENT *ve)
{
	long secs, psecs;

	secs = k->ki_p->ki_rusage.ru_stime.tv_sec;
	psecs = k->ki_p->ki_rusage.ru_stime.tv_usec;
	if (sumrusage) {
		secs += k->ki_p->ki_childstime.tv_sec;
		psecs += k->ki_p->ki_childstime.tv_usec;
	}
	return (printtime(k, ve, secs, psecs));
}

char *
usertime(KINFO *k, VARENT *ve)
{
	long secs, psecs;

	secs = k->ki_p->ki_rusage.ru_utime.tv_sec;
	psecs = k->ki_p->ki_rusage.ru_utime.tv_usec;
	if (sumrusage) {
		secs += k->ki_p->ki_childutime.tv_sec;
		psecs += k->ki_p->ki_childutime.tv_usec;
	}
	return (printtime(k, ve, secs, psecs));
}

char *
elapsed(KINFO *k, VARENT *ve __unused)
{
	time_t val;
	int days, hours, mins, secs;
	char *str;

	if (!k->ki_valid)
		return (NULL);
	val = now - k->ki_p->ki_start.tv_sec;
	days = val / (24 * 60 * 60);
	val %= 24 * 60 * 60;
	hours = val / (60 * 60);
	val %= 60 * 60;
	mins = val / 60;
	secs = val % 60;
	if (days != 0)
		asprintf(&str, "%3d-%02d:%02d:%02d", days, hours, mins, secs);
	else if (hours != 0)
		asprintf(&str, "%02d:%02d:%02d", hours, mins, secs);
	else
		asprintf(&str, "%02d:%02d", mins, secs);

	return (str);
}

char *
elapseds(KINFO *k, VARENT *ve __unused)
{
	time_t val;
	char *str;

	if (!k->ki_valid)
		return (NULL);
	val = now - k->ki_p->ki_start.tv_sec;
	asprintf(&str, "%jd", (intmax_t)val);
	return (str);
}

double
getpcpu(const KINFO *k)
{
	static int failure;

	if (!nlistread)
		failure = donlist();
	if (failure)
		return (0.0);

#define	fxtofl(fixpt)	((double)(fixpt) / fscale)

	/* XXX - I don't like this */
	if (k->ki_p->ki_swtime == 0 || (k->ki_p->ki_flag & P_INMEM) == 0)
		return (0.0);
	if (rawcpu)
		return (100.0 * fxtofl(k->ki_p->ki_pctcpu));
	return (100.0 * fxtofl(k->ki_p->ki_pctcpu) /
		(1.0 - exp(k->ki_p->ki_swtime * log(fxtofl(ccpu)))));
}

char *
pcpu(KINFO *k, VARENT *ve __unused)
{
	char *str;

	asprintf(&str, "%.1f", getpcpu(k));
	return (str);
}

static double
getpmem(KINFO *k)
{
	static int failure;
	double fracmem;

	if (!nlistread)
		failure = donlist();
	if (failure)
		return (0.0);

	if ((k->ki_p->ki_flag & P_INMEM) == 0)
		return (0.0);
	/* XXX want pmap ptpages, segtab, etc. (per architecture) */
	/* XXX don't have info about shared */
	fracmem = ((double)k->ki_p->ki_rssize) / mempages;
	return (100.0 * fracmem);
}

char *
pmem(KINFO *k, VARENT *ve __unused)
{
	char *str;

	asprintf(&str, "%.1f", getpmem(k));
	return (str);
}

char *
pagein(KINFO *k, VARENT *ve __unused)
{
	char *str;

	asprintf(&str, "%ld", k->ki_valid ? k->ki_p->ki_rusage.ru_majflt : 0);
	return (str);
}

/* ARGSUSED */
char *
maxrss(KINFO *k __unused, VARENT *ve __unused)
{

	/* XXX not yet */
	return (NULL);
}

char *
priorityr(KINFO *k, VARENT *ve __unused)
{
	struct priority *lpri;
	char *str;
	unsigned class, level;

	lpri = &k->ki_p->ki_pri;
	class = lpri->pri_class;
	level = lpri->pri_level;
	switch (class) {
	case PRI_ITHD:
		asprintf(&str, "intr:%u", level);
		break;
	case PRI_REALTIME:
		asprintf(&str, "real:%u", level);
		break;
	case PRI_TIMESHARE:
		asprintf(&str, "normal");
		break;
	case PRI_IDLE:
		asprintf(&str, "idle:%u", level);
		break;
	default:
		asprintf(&str, "%u:%u", class, level);
		break;
	}
	return (str);
}

/*
 * Generic output routines.  Print fields from various prototype
 * structures.
 */
static char *
printval(void *bp, VAR *v)
{
	static char ofmt[32] = "%";
	const char *fcp;
	char *cp, *str;

	cp = ofmt + 1;
	fcp = v->fmt;
	while ((*cp++ = *fcp++));

#define	CHKINF127(n)	(((n) > 127) && (v->flag & INF127) ? 127 : (n))

	switch (v->type) {
	case CHAR:
		(void)asprintf(&str, ofmt, *(char *)bp);
		break;
	case UCHAR:
		(void)asprintf(&str, ofmt, *(u_char *)bp);
		break;
	case SHORT:
		(void)asprintf(&str, ofmt, *(short *)bp);
		break;
	case USHORT:
		(void)asprintf(&str, ofmt, *(u_short *)bp);
		break;
	case INT:
		(void)asprintf(&str, ofmt, *(int *)bp);
		break;
	case UINT:
		(void)asprintf(&str, ofmt, CHKINF127(*(u_int *)bp));
		break;
	case LONG:
		(void)asprintf(&str, ofmt, *(long *)bp);
		break;
	case ULONG:
		(void)asprintf(&str, ofmt, *(u_long *)bp);
		break;
	case KPTR:
		(void)asprintf(&str, ofmt, *(u_long *)bp);
		break;
	case PGTOK:
		(void)asprintf(&str, ofmt, ps_pgtok(*(u_long *)bp));
		break;
	}

	return (str);
}

char *
kvar(KINFO *k, VARENT *ve)
{
	VAR *v;

	v = ve->var;
	return (printval((char *)((char *)k->ki_p + v->off), v));
}

char *
rvar(KINFO *k, VARENT *ve)
{
	VAR *v;

	v = ve->var;
	if (!k->ki_valid)
		return (NULL);
	return (printval((char *)((char *)(&k->ki_p->ki_rusage) + v->off), v));
}

char *
emulname(KINFO *k, VARENT *ve __unused)
{

	return (strdup(k->ki_p->ki_emul));
}

char *
label(KINFO *k, VARENT *ve __unused)
{
	char *string;
	mac_t proclabel;
	int error;

	string = NULL;
	if (mac_prepare_process_label(&proclabel) == -1) {
		xo_warn("mac_prepare_process_label");
		goto out;
	}
	error = mac_get_pid(k->ki_p->ki_pid, proclabel);
	if (error == 0) {
		if (mac_to_text(proclabel, &string) == -1)
			string = NULL;
	}
	mac_free(proclabel);
out:
	return (string);
}

char *
loginclass(KINFO *k, VARENT *ve __unused)
{

	/*
	 * Don't display login class for system processes;
	 * login classes are used for resource limits,
	 * and limits don't apply to system processes.
	 */
	if (k->ki_p->ki_flag & P_SYSTEM) {
		return (strdup("-"));
	}
	return (strdup(k->ki_p->ki_loginclass));
}

char *
jailname(KINFO *k, VARENT *ve __unused)
{
	char *name;

	if (k->ki_p->ki_jid == 0)
		return (strdup("-"));
	name = jail_getname(k->ki_p->ki_jid);
	if (name == NULL)
		return (strdup("-"));
	return (name);
}
