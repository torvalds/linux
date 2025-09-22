/*	$OpenBSD: ps.c,v 1.84 2025/07/02 13:24:48 deraadt Exp $	*/
/*	$NetBSD: ps.c,v 1.15 1995/05/18 20:33:25 mycroft Exp $	*/

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

#include <sys/param.h>	/* NODEV */
#include <sys/types.h>
#include <sys/signal.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/proc.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <kvm.h>
#include <locale.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "ps.h"

extern char *__progname;

struct varent *vhead;

int	sumrusage;		/* -S */
int	termwidth;		/* width of screen (0 == infinity) */
int	totwidth;		/* calculated width of requested variables */
int pagesize;

int	needcomm, needenv, commandonly;

enum sort { DEFAULT, SORTMEM, SORTCPU } sortby = DEFAULT;

static char	*kludge_oldps_options(char *);
static int	 pscomp(const void *, const void *);
static void	 scanvars(struct kinfo_proc *kp, size_t nentries);
static void	 forest_sort(struct pinfo *, int);
static void	 usage(void);

char dfmt[] = "pid tt state time command";
char tfmt[] = "pid tid tt state time command";
char jfmt[] = "user pid ppid pgid sess jobc state tt time command";
char lfmt[] = "uid pid ppid cpu pri nice vsz rss wchan state tt time command";
char   o1[] = "pid";
char   o2[] = "tt state time command";
char ufmt[] = "user pid %cpu %mem vsz rss tt state start time command";
char vfmt[] = "pid state time sl re pagein vsz rss lim tsiz %cpu %mem command";

kvm_t *kd;
int kvm_sysctl_only;

int
main(int argc, char *argv[])
{
	struct kinfo_proc *kp;
	struct pinfo *pinfo;
	struct varent *vent;
	struct winsize ws;
	dev_t ttydev;
	pid_t pid;
	uid_t uid;
	int all, ch, flag, i, fmt, lineno, nentries;
	int prtheader, showthreads, wflag, kflag, what, Uflag, xflg;
	int forest, rval = 0;
	char *nlistf, *memf, *swapf, *cols, errbuf[_POSIX2_LINE_MAX];

	setlocale(LC_CTYPE, "");

	termwidth = 0;
	if ((cols = getenv("COLUMNS")) != NULL)
		termwidth = strtonum(cols, 1, INT_MAX, NULL);
	if (termwidth == 0 &&
	    (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 ||
	    ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 ||
	    ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) &&
	    ws.ws_col > 0)
		termwidth = ws.ws_col - 1;
	if (termwidth == 0)
		termwidth = 79;
	pagesize = getpagesize();

	if (argc > 1)
		argv[1] = kludge_oldps_options(argv[1]);

	all = fmt = prtheader = showthreads = wflag = kflag = Uflag = xflg = 0;
	pid = -1;
	uid = 0;
	forest = 0;
	ttydev = NODEV;
	memf = nlistf = swapf = NULL;
	while ((ch = getopt(argc, argv,
	    "AaCcefgHhjkLlM:mN:O:o:p:rSTt:U:uvW:wx")) != -1)
		switch (ch) {
		case 'A':
			all = 1;
			xflg = 1;
			break;
		case 'a':
			all = 1;
			break;
		case 'C':
			break;			/* no-op */
		case 'c':
			commandonly = 1;
			break;
		case 'e':			/* XXX set ufmt */
			needenv = 1;
			break;
		case 'f':
			forest = 1;
			break;
		case 'g':
			break;			/* no-op */
		case 'H':
			showthreads = 1;
			break;
		case 'h':
			prtheader = ws.ws_row > 5 ? ws.ws_row : 22;
			break;
		case 'j':
			rval |= parsefmt(jfmt);
			fmt = 1;
			jfmt[0] = '\0';
			break;
		case 'k':
			kflag = 1;
			break;
		case 'L':
			showkey();
			exit(0);
		case 'l':
			rval |= parsefmt(lfmt);
			fmt = 1;
			lfmt[0] = '\0';
			break;
		case 'M':
			memf = optarg;
			break;
		case 'm':
			sortby = SORTMEM;
			break;
		case 'N':
			nlistf = optarg;
			break;
		case 'O':
			rval |= parsefmt(o1);
			rval |= parsefmt(optarg);
			rval |= parsefmt(o2);
			o1[0] = o2[0] = '\0';
			fmt = 1;
			break;
		case 'o':
			rval |= parsefmt(optarg);
			fmt = 1;
			break;
		case 'p':
			pid = atol(optarg);
			xflg = 1;
			break;
		case 'r':
			sortby = SORTCPU;
			break;
		case 'S':
			sumrusage = 1;
			break;
		case 'T':
			if ((optarg = ttyname(STDIN_FILENO)) == NULL)
				errx(1, "stdin: not a terminal");
			/* FALLTHROUGH */
		case 't': {
			struct stat sb;
			char *ttypath, pathbuf[PATH_MAX];

			if (strcmp(optarg, "co") == 0)
				ttypath = _PATH_CONSOLE;
			else if (*optarg != '/') {
				int r = snprintf(pathbuf, sizeof(pathbuf), "%s%s",
				    _PATH_TTY, optarg);
				if (r < 0 || r > sizeof(pathbuf))
					errx(1, "%s: too long\n", optarg);
				ttypath = pathbuf;
			} else
				ttypath = optarg;
			if (stat(ttypath, &sb) == -1)
				err(1, "%s", ttypath);
			if (!S_ISCHR(sb.st_mode))
				errx(1, "%s: not a terminal", ttypath);
			ttydev = sb.st_rdev;
			break;
		}
		case 'U': {
			int found = 0;

			if (uid_from_user(optarg, &uid) == 0)
				found = 1;
			else {
				const char *errstr;

				uid = strtonum(optarg, 0, UID_MAX, &errstr);
				if (errstr == NULL &&
				    user_from_uid(uid, 1) != NULL)
					found = 1;
			}
			if (!found)
				errx(1, "%s: unknown user", optarg);
			Uflag = xflg = 1;
			break;
		}
		case 'u':
			rval |= parsefmt(ufmt);
			sortby = SORTCPU;
			fmt = 1;
			ufmt[0] = '\0';
			break;
		case 'v':
			rval |= parsefmt(vfmt);
			sortby = SORTMEM;
			fmt = 1;
			vfmt[0] = '\0';
			break;
		case 'W':
			swapf = optarg;
			break;
		case 'w':
			if (wflag)
				termwidth = UNLIMITED;
			else if (termwidth < 131)
				termwidth = 131;
			wflag = 1;
			break;
		case 'x':
			xflg = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

#define	BACKWARD_COMPATIBILITY
#ifdef	BACKWARD_COMPATIBILITY
	if (*argv) {
		nlistf = *argv;
		if (*++argv) {
			memf = *argv;
			if (*++argv)
				swapf = *argv;
		}
	}
#endif

	if (nlistf == NULL && memf == NULL && swapf == NULL) {
		kd = kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, errbuf);
		kvm_sysctl_only = 1;
	} else {
		kd = kvm_openfiles(nlistf, memf, swapf, O_RDONLY, errbuf);
	}
	if (kd == NULL)
		errx(1, "%s", errbuf);

	if (unveil(_PATH_DEVDB, "r") == -1 && errno != ENOENT)
		err(1, "unveil %s", _PATH_DEVDB);
	if (unveil(_PATH_DEV, "r") == -1 && errno != ENOENT)
		err(1, "unveil %s", _PATH_DEV);
	if (swapf)
		if (unveil(swapf, "r") == -1)
			err(1, "unveil %s", swapf);
	if (nlistf)
		if (unveil(nlistf, "r") == -1)
			err(1, "unveil %s", nlistf);
	if (memf)
		if (unveil(memf, "r") == -1)
			err(1, "unveil %s", memf);
	if (pledge("stdio rpath getpw ps", NULL) == -1)
		err(1, "pledge");

	if (!fmt) {
		if (showthreads)
			rval |= parsefmt(tfmt);
		else
			rval |= parsefmt(dfmt);
	}

	/* XXX - should be cleaner */
	if (!all && ttydev == NODEV && pid == -1 && !Uflag) {
		uid = getuid();
		Uflag = 1;
	}

	rval |= getkernvars();

	/*
	 * get proc list
	 */
	if (Uflag) {
		what = KERN_PROC_UID;
		flag = uid;
	} else if (ttydev != NODEV) {
		what = KERN_PROC_TTY;
		flag = ttydev;
	} else if (pid != -1) {
		what = KERN_PROC_PID;
		flag = pid;
	} else if (kflag) {
		what = KERN_PROC_KTHREAD;
		flag = 0;
	} else {
		what = KERN_PROC_ALL;
		flag = 0;
	}
	if (showthreads)
		what |= KERN_PROC_SHOW_THREADS;

	/*
	 * select procs
	 */
	kp = kvm_getprocs(kd, what, flag, sizeof(*kp), &nentries);
	if (kp == NULL)
		errx(1, "%s", kvm_geterr(kd));

	/*
	 * scan requested variables, noting what structures are needed,
	 * and adjusting header widths as appropriate.
	 */
	scanvars(kp, nentries);

	/*
	 * print header
	 */
	printheader();
	if (nentries == 0)
		exit(1);

	if ((pinfo = calloc(nentries, sizeof(struct pinfo))) == NULL)
		err(1, NULL);
	for (i = 0; i < nentries; i++)
		pinfo[i].ki = &kp[i];
	qsort(pinfo, nentries, sizeof(struct pinfo), pscomp);

	if (forest)
		forest_sort(pinfo, nentries);

	/*
	 * for each proc, call each variable output function.
	 */
	for (i = lineno = 0; i < nentries; i++) {
		if (xflg == 0 && ((int)pinfo[i].ki->p_tdev == NODEV ||
		    (pinfo[i].ki->p_psflags & PS_CONTROLT ) == 0))
			continue;
		if (showthreads && pinfo[i].ki->p_tid == -1)
			continue;
		for (vent = vhead; vent; vent = vent->next) {
			(vent->var->oproc)(&pinfo[i], vent);
			if (vent->next != NULL)
				(void)putchar(' ');
		}
		(void)putchar('\n');
		if (prtheader && lineno++ == prtheader - 4) {
			(void)putchar('\n');
			printheader();
			lineno = 0;
		}
	}
	exit(rval);
}

static void
scanvars(struct kinfo_proc *kp, size_t nentries)
{
	struct varent *vent;
	VAR *v;
	int i;
	int vszbump = 0, rssbump = 0;

#define pgtok(a)    (((unsigned long long)(a)*pagesize)/1024)
	for (i = 0; i < nentries; i++) {
		struct kinfo_proc *ki = &kp[i];
		if (vszbump == 0 && pgtok(ki->p_vm_dsize + ki->p_vm_ssize + ki->p_vm_tsize) >= 100000)
			vszbump = 1;
		if (vszbump == 1 && pgtok(ki->p_vm_dsize + ki->p_vm_ssize + ki->p_vm_tsize) >= 1000000)
			vszbump = 2;
		if (rssbump == 0 && pgtok(ki->p_vm_rssize) >= 100000)
			rssbump = 1;
		if (rssbump == 1 && pgtok(ki->p_vm_rssize) >= 1000000)
			rssbump = 2;
	}
	for (vent = vhead; vent; vent = vent->next) {
		v = vent->var;
		if (strcmp(v->name, "vsz") == 0)
			v->width += vszbump;
		if (strcmp(v->name, "rss") == 0)
			v->width += rssbump;
		i = strlen(v->header);
		if (v->width < i)
			v->width = i;
		totwidth += v->width + 1;	/* +1 for space */
		if (v->flag & COMM)
			needcomm = 1;
	}
	totwidth--;
}

static int
pscomp(const void *v1, const void *v2)
{
	const struct pinfo *p1 = (const struct pinfo *)v1;
	const struct pinfo *p2 = (const struct pinfo *)v2;
	const struct kinfo_proc *kp1 = p1->ki;
	const struct kinfo_proc *kp2 = p2->ki;
	int i;
#define VSIZE(k) ((k)->p_vm_dsize + (k)->p_vm_ssize + (k)->p_vm_tsize)

	if (sortby == SORTCPU && (i = getpcpu(kp2) - getpcpu(kp1)) != 0)
		return (i);
	if (sortby == SORTMEM && (i = VSIZE(kp2) - VSIZE(kp1)) != 0)
		return (i);
	if ((i = kp1->p_tdev - kp2->p_tdev) == 0 &&
	    (i = kp1->p_ustart_sec - kp2->p_ustart_sec) == 0)
		i = kp1->p_ustart_usec - kp2->p_ustart_usec;
	return (i);
}

/*
 * ICK (all for getopt), would rather hide the ugliness
 * here than taint the main code.
 *
 *  ps foo -> ps -foo
 *  ps 34 -> ps -p34
 *
 * The old convention that 't' with no trailing tty arg means the users
 * tty, is only supported if argv[1] doesn't begin with a '-'.  This same
 * feature is available with the option 'T', which takes no argument.
 */
static char *
kludge_oldps_options(char *s)
{
	size_t len;
	char *newopts, *ns, *cp;

	len = strlen(s);
	if ((newopts = ns = malloc(2 + len + 1)) == NULL)
		err(1, NULL);
	/*
	 * options begin with '-'
	 */
	if (*s != '-')
		*ns++ = '-';	/* add option flag */

	/*
	 * gaze to end of argv[1]
	 */
	cp = s + len - 1;
	/*
	 * if last letter is a 't' flag with no argument (in the context
	 * of the oldps options -- option string NOT starting with a '-' --
	 * then convert to 'T' (meaning *this* terminal, i.e. ttyname(0)).
	 */
	if (*cp == 't' && *s != '-')
		*cp = 'T';
	else {
		/*
		 * otherwise check for trailing number, which *may* be a
		 * pid.
		 */
		while (cp >= s && isdigit((unsigned char)*cp))
			--cp;
	}
	cp++;
	memmove(ns, s, (size_t)(cp - s));	/* copy up to trailing number */
	ns += cp - s;
	/*
	 * if there's a trailing number, and not a preceding 'p' (pid),
	 * 't' (tty) or 'U' (user) flag,
	 * then assume it's a pid and insert a 'p' flag.
	 */
	if (isdigit((unsigned char)*cp) &&
	    (cp == s || (cp[-1] != 't' && cp[-1] != 'p' && cp[-1] != 'U' &&
	    (cp - 1 == s || cp[-2] != 't'))))
		*ns++ = 'p';
	/* and append the number */
	(void)strlcpy(ns, cp, newopts + len + 3 - ns);

	return (newopts);
}

static void
forest_sort(struct pinfo *ki, int items)
{
	int dst, lvl, maxlvl, n, ndst, nsrc, siblings, src;
	unsigned char *path;
	struct pinfo kn;

	/*
	 * First, sort the entries by forest, tracking the forest
	 * depth in the level field.
	 */
	src = 0;
	maxlvl = 0;
	while (src < items) {
		if (ki[src].level) {
			src++;
			continue;
		}
		for (nsrc = 1; src + nsrc < items; nsrc++)
			if (!ki[src + nsrc].level)
				break;

		for (dst = 0; dst < items; dst++) {
			if (ki[dst].ki->p_pid == ki[src].ki->p_pid)
				continue;
			if (ki[dst].ki->p_pid == ki[src].ki->p_ppid)
				break;
		}

		if (dst == items) {
			src += nsrc;
			continue;
		}

		for (ndst = 1; dst + ndst < items; ndst++)
			if (ki[dst + ndst].level <= ki[dst].level)
				break;

		for (n = src; n < src + nsrc; n++) {
			ki[n].level += ki[dst].level + 1;
			if (maxlvl < ki[n].level)
				maxlvl = ki[n].level;
		}

		while (nsrc) {
			if (src < dst) {
				kn = ki[src];
				memmove(ki + src, ki + src + 1,
				    (dst - src + ndst - 1) * sizeof *ki);
				ki[dst + ndst - 1] = kn;
				nsrc--;
				dst--;
				ndst++;
			} else if (src != dst + ndst) {
				kn = ki[src];
				memmove(ki + dst + ndst + 1, ki + dst + ndst,
				    (src - dst - ndst) * sizeof *ki);
				ki[dst + ndst] = kn;
				ndst++;
				nsrc--;
				src++;
			} else {
				ndst += nsrc;
				src += nsrc;
				nsrc = 0;
			}
		}
	}
	/*
	 * Now populate prefix (instead of level) with the command
	 * prefix used to show descendancies.
	 */
	path = calloc(1, (maxlvl + 7) / 8);
	if (path == NULL)
		err(1, NULL);

	for (src = 0; src < items; src++) {
		if ((lvl = ki[src].level) == 0) {
			ki[src].prefix = NULL;
			continue;
		}

		if ((ki[src].prefix = malloc(lvl * 2 + 1)) == NULL)
			err(1, NULL);

		for (n = 0; n < lvl - 2; n++) {
			ki[src].prefix[n * 2] =
			    path[n / 8] & 1 << (n % 8) ? '|' : ' ';
			ki[src].prefix[n * 2 + 1] = ' ';

		}
		if (n == lvl - 2) {
			/* Have I any more siblings? */
			for (siblings = 0, dst = src + 1; dst < items; dst++) {
				if (ki[dst].level > lvl)
					continue;
				if (ki[dst].level == lvl)
					siblings = 1;
				break;
			}
			if (siblings)
				path[n / 8] |= 1 << (n % 8);
			else
				path[n / 8] &= ~(1 << (n % 8));
			ki[src].prefix[n * 2] = siblings ? '|' : '`';
			ki[src].prefix[n * 2 + 1] = '-';
			n++;
		}
		strlcpy(ki[src].prefix + n * 2, "- ", (lvl - n) * 2 + 1);
	}
	free(path);
}

static void
usage(void)
{
	fprintf(stderr, "usage: %s [[-]AacefHhjkLlmrSTuvwx] [-M core]"
	    " [-N system] [-O fmt] [-o fmt]\n", __progname);
	fprintf(stderr, "%-*s[-p pid] [-t tty] [-U user] [-W swap]\n",
	    (int)strlen(__progname) + 8, "");
	exit(1);
}
