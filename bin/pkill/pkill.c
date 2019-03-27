/*	$NetBSD: pkill.c,v 1.16 2005/10/10 22:13:20 kleink Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * Copyright (c) 2005 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/user.h>

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <paths.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <regex.h>
#include <ctype.h>
#include <fcntl.h>
#include <kvm.h>
#include <err.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <locale.h>
#include <jail.h>

#define	STATUS_MATCH	0
#define	STATUS_NOMATCH	1
#define	STATUS_BADUSAGE	2
#define	STATUS_ERROR	3

#define	MIN_PID	5
#define	MAX_PID	99999

/* Ignore system-processes (if '-S' flag is not specified) and myself. */
#define	PSKIP(kp)	((kp)->ki_pid == mypid ||			\
			 (!kthreads && ((kp)->ki_flag & P_KPROC) != 0))

enum listtype {
	LT_GENERIC,
	LT_USER,
	LT_GROUP,
	LT_TTY,
	LT_PGRP,
	LT_JAIL,
	LT_SID,
	LT_CLASS
};

struct list {
	SLIST_ENTRY(list) li_chain;
	long	li_number;
	char	*li_name;
};

SLIST_HEAD(listhead, list);

static struct kinfo_proc *plist;
static char	*selected;
static const char *delim = "\n";
static int	nproc;
static int	pgrep;
static int	signum = SIGTERM;
static int	newest;
static int	oldest;
static int	interactive;
static int	inverse;
static int	longfmt;
static int	matchargs;
static int	fullmatch;
static int	kthreads;
static int	cflags = REG_EXTENDED;
static int	quiet;
static kvm_t	*kd;
static pid_t	mypid;

static struct listhead euidlist = SLIST_HEAD_INITIALIZER(euidlist);
static struct listhead ruidlist = SLIST_HEAD_INITIALIZER(ruidlist);
static struct listhead rgidlist = SLIST_HEAD_INITIALIZER(rgidlist);
static struct listhead pgrplist = SLIST_HEAD_INITIALIZER(pgrplist);
static struct listhead ppidlist = SLIST_HEAD_INITIALIZER(ppidlist);
static struct listhead tdevlist = SLIST_HEAD_INITIALIZER(tdevlist);
static struct listhead sidlist = SLIST_HEAD_INITIALIZER(sidlist);
static struct listhead jidlist = SLIST_HEAD_INITIALIZER(jidlist);
static struct listhead classlist = SLIST_HEAD_INITIALIZER(classlist);

static void	usage(void) __attribute__((__noreturn__));
static int	killact(const struct kinfo_proc *);
static int	grepact(const struct kinfo_proc *);
static void	makelist(struct listhead *, enum listtype, char *);
static int	takepid(const char *, int);

int
main(int argc, char **argv)
{
	char buf[_POSIX2_LINE_MAX], *mstr, **pargv, *p, *q, *pidfile;
	const char *execf, *coref;
	int ancestors, debug_opt, did_action;
	int i, ch, bestidx, rv, criteria, pidfromfile, pidfilelock;
	size_t jsz;
	int (*action)(const struct kinfo_proc *);
	struct kinfo_proc *kp;
	struct list *li;
	struct timeval best_tval;
	regex_t reg;
	regmatch_t regmatch;
	pid_t pid;

	setlocale(LC_ALL, "");

	if (strcmp(getprogname(), "pgrep") == 0) {
		action = grepact;
		pgrep = 1;
	} else {
		action = killact;
		p = argv[1];

		if (argc > 1 && p[0] == '-') {
			p++;
			i = (int)strtol(p, &q, 10);
			if (*q == '\0') {
				signum = i;
				argv++;
				argc--;
			} else {
				if (strncasecmp(p, "SIG", 3) == 0)
					p += 3;
				for (i = 1; i < NSIG; i++)
					if (strcasecmp(sys_signame[i], p) == 0)
						break;
				if (i != NSIG) {
					signum = i;
					argv++;
					argc--;
				}
			}
		}
	}

	ancestors = 0;
	criteria = 0;
	debug_opt = 0;
	pidfile = NULL;
	pidfilelock = 0;
	quiet = 0;
	execf = NULL;
	coref = _PATH_DEVNULL;

	while ((ch = getopt(argc, argv, "DF:G:ILM:N:P:SU:ac:d:fg:ij:lnoqs:t:u:vx")) != -1)
		switch (ch) {
		case 'D':
			debug_opt++;
			break;
		case 'F':
			pidfile = optarg;
			criteria = 1;
			break;
		case 'G':
			makelist(&rgidlist, LT_GROUP, optarg);
			criteria = 1;
			break;
		case 'I':
			if (pgrep)
				usage();
			interactive = 1;
			break;
		case 'L':
			pidfilelock = 1;
			break;
		case 'M':
			coref = optarg;
			break;
		case 'N':
			execf = optarg;
			break;
		case 'P':
			makelist(&ppidlist, LT_GENERIC, optarg);
			criteria = 1;
			break;
		case 'S':
			if (!pgrep)
				usage();
			kthreads = 1;
			break;
		case 'U':
			makelist(&ruidlist, LT_USER, optarg);
			criteria = 1;
			break;
		case 'a':
			ancestors++;
			break;
		case 'c':
			makelist(&classlist, LT_CLASS, optarg);
			criteria = 1;
			break;
		case 'd':
			if (!pgrep)
				usage();
			delim = optarg;
			break;
		case 'f':
			matchargs = 1;
			break;
		case 'g':
			makelist(&pgrplist, LT_PGRP, optarg);
			criteria = 1;
			break;
		case 'i':
			cflags |= REG_ICASE;
			break;
		case 'j':
			makelist(&jidlist, LT_JAIL, optarg);
			criteria = 1;
			break;
		case 'l':
			longfmt = 1;
			break;
		case 'n':
			newest = 1;
			criteria = 1;
			break;
		case 'o':
			oldest = 1;
			criteria = 1;
			break;
		case 'q':
			if (!pgrep)
				usage();
			quiet = 1;
			break;
		case 's':
			makelist(&sidlist, LT_SID, optarg);
			criteria = 1;
			break;
		case 't':
			makelist(&tdevlist, LT_TTY, optarg);
			criteria = 1;
			break;
		case 'u':
			makelist(&euidlist, LT_USER, optarg);
			criteria = 1;
			break;
		case 'v':
			inverse = 1;
			break;
		case 'x':
			fullmatch = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}

	argc -= optind;
	argv += optind;
	if (argc != 0)
		criteria = 1;
	if (!criteria)
		usage();
	if (newest && oldest)
		errx(STATUS_ERROR, "Options -n and -o are mutually exclusive");
	if (pidfile != NULL)
		pidfromfile = takepid(pidfile, pidfilelock);
	else {
		if (pidfilelock) {
			errx(STATUS_ERROR,
			    "Option -L doesn't make sense without -F");
		}
		pidfromfile = -1;
	}

	mypid = getpid();

	/*
	 * Retrieve the list of running processes from the kernel.
	 */
	kd = kvm_openfiles(execf, coref, NULL, O_RDONLY, buf);
	if (kd == NULL)
		errx(STATUS_ERROR, "Cannot open kernel files (%s)", buf);

	/*
	 * Use KERN_PROC_PROC instead of KERN_PROC_ALL, since we
	 * just want processes and not individual kernel threads.
	 */
	if (pidfromfile >= 0)
		plist = kvm_getprocs(kd, KERN_PROC_PID, pidfromfile, &nproc);
	else
		plist = kvm_getprocs(kd, KERN_PROC_PROC, 0, &nproc);
	if (plist == NULL) {
		errx(STATUS_ERROR, "Cannot get process list (%s)",
		    kvm_geterr(kd));
	}

	/*
	 * Allocate memory which will be used to keep track of the
	 * selection.
	 */
	if ((selected = malloc(nproc)) == NULL) {
		err(STATUS_ERROR, "Cannot allocate memory for %d processes",
		    nproc);
	}
	memset(selected, 0, nproc);

	/*
	 * Refine the selection.
	 */
	for (; *argv != NULL; argv++) {
		if ((rv = regcomp(&reg, *argv, cflags)) != 0) {
			regerror(rv, &reg, buf, sizeof(buf));
			errx(STATUS_BADUSAGE,
			    "Cannot compile regular expression `%s' (%s)",
			    *argv, buf);
		}

		for (i = 0, kp = plist; i < nproc; i++, kp++) {
			if (PSKIP(kp)) {
				if (debug_opt > 0)
				    fprintf(stderr, "* Skipped %5d %3d %s\n",
					kp->ki_pid, kp->ki_uid, kp->ki_comm);
				continue;
			}

			if (matchargs &&
			    (pargv = kvm_getargv(kd, kp, 0)) != NULL) {
				jsz = 0;
				while (jsz < sizeof(buf) && *pargv != NULL) {
					jsz += snprintf(buf + jsz,
					    sizeof(buf) - jsz,
					    pargv[1] != NULL ? "%s " : "%s",
					    pargv[0]);
					pargv++;
				}
				mstr = buf;
			} else
				mstr = kp->ki_comm;

			rv = regexec(&reg, mstr, 1, &regmatch, 0);
			if (rv == 0) {
				if (fullmatch) {
					if (regmatch.rm_so == 0 &&
					    regmatch.rm_eo ==
					    (off_t)strlen(mstr))
						selected[i] = 1;
				} else
					selected[i] = 1;
			} else if (rv != REG_NOMATCH) {
				regerror(rv, &reg, buf, sizeof(buf));
				errx(STATUS_ERROR,
				    "Regular expression evaluation error (%s)",
				    buf);
			}
			if (debug_opt > 1) {
				const char *rv_res = "NoMatch";
				if (selected[i])
					rv_res = "Matched";
				fprintf(stderr, "* %s %5d %3d %s\n", rv_res,
				    kp->ki_pid, kp->ki_uid, mstr);
			}
		}

		regfree(&reg);
	}

	for (i = 0, kp = plist; i < nproc; i++, kp++) {
		if (PSKIP(kp))
			continue;

		if (pidfromfile >= 0 && kp->ki_pid != pidfromfile) {
			selected[i] = 0;
			continue;
		}

		SLIST_FOREACH(li, &ruidlist, li_chain)
			if (kp->ki_ruid == (uid_t)li->li_number)
				break;
		if (SLIST_FIRST(&ruidlist) != NULL && li == NULL) {
			selected[i] = 0;
			continue;
		}

		SLIST_FOREACH(li, &rgidlist, li_chain)
			if (kp->ki_rgid == (gid_t)li->li_number)
				break;
		if (SLIST_FIRST(&rgidlist) != NULL && li == NULL) {
			selected[i] = 0;
			continue;
		}

		SLIST_FOREACH(li, &euidlist, li_chain)
			if (kp->ki_uid == (uid_t)li->li_number)
				break;
		if (SLIST_FIRST(&euidlist) != NULL && li == NULL) {
			selected[i] = 0;
			continue;
		}

		SLIST_FOREACH(li, &ppidlist, li_chain)
			if (kp->ki_ppid == (pid_t)li->li_number)
				break;
		if (SLIST_FIRST(&ppidlist) != NULL && li == NULL) {
			selected[i] = 0;
			continue;
		}

		SLIST_FOREACH(li, &pgrplist, li_chain)
			if (kp->ki_pgid == (pid_t)li->li_number)
				break;
		if (SLIST_FIRST(&pgrplist) != NULL && li == NULL) {
			selected[i] = 0;
			continue;
		}

		SLIST_FOREACH(li, &tdevlist, li_chain) {
			if (li->li_number == -1 &&
			    (kp->ki_flag & P_CONTROLT) == 0)
				break;
			if (kp->ki_tdev == (dev_t)li->li_number)
				break;
		}
		if (SLIST_FIRST(&tdevlist) != NULL && li == NULL) {
			selected[i] = 0;
			continue;
		}

		SLIST_FOREACH(li, &sidlist, li_chain)
			if (kp->ki_sid == (pid_t)li->li_number)
				break;
		if (SLIST_FIRST(&sidlist) != NULL && li == NULL) {
			selected[i] = 0;
			continue;
		}

		SLIST_FOREACH(li, &jidlist, li_chain) {
			/* A particular jail ID, including 0 (not in jail) */
			if (kp->ki_jid == (int)li->li_number)
				break;
			/* Any jail */
			if (kp->ki_jid > 0 && li->li_number == -1)
				break;
		}
		if (SLIST_FIRST(&jidlist) != NULL && li == NULL) {
			selected[i] = 0;
			continue;
		}

		SLIST_FOREACH(li, &classlist, li_chain) {
			/*
			 * We skip P_SYSTEM processes to match ps(1) output.
			 */
			if ((kp->ki_flag & P_SYSTEM) == 0 &&
			    strcmp(kp->ki_loginclass, li->li_name) == 0)
				break;
		}
		if (SLIST_FIRST(&classlist) != NULL && li == NULL) {
			selected[i] = 0;
			continue;
		}

		if (argc == 0)
			selected[i] = 1;
	}

	if (!ancestors) {
		pid = mypid;
		while (pid) {
			for (i = 0, kp = plist; i < nproc; i++, kp++) {
				if (PSKIP(kp))
					continue;
				if (kp->ki_pid == pid) {
					selected[i] = 0;
					pid = kp->ki_ppid;
					break;
				}
			}
			if (i == nproc) {
				if (pid == mypid)
					pid = getppid();
				else
					break;	/* Maybe we're in a jail ? */
			}
		}
	}

	if (newest || oldest) {
		best_tval.tv_sec = 0;
		best_tval.tv_usec = 0;
		bestidx = -1;

		for (i = 0, kp = plist; i < nproc; i++, kp++) {
			if (!selected[i])
				continue;
			if (bestidx == -1) {
				/* The first entry of the list which matched. */
				;
			} else if (timercmp(&kp->ki_start, &best_tval, >)) {
				/* This entry is newer than previous "best". */
				if (oldest)	/* but we want the oldest */
					continue;
			} else {
				/* This entry is older than previous "best". */
				if (newest)	/* but we want the newest */
					continue;
			}
			/* This entry is better than previous "best" entry. */
			best_tval.tv_sec = kp->ki_start.tv_sec;
			best_tval.tv_usec = kp->ki_start.tv_usec;
			bestidx = i;
		}

		memset(selected, 0, nproc);
		if (bestidx != -1)
			selected[bestidx] = 1;
	}

	/*
	 * Take the appropriate action for each matched process, if any.
	 */
	did_action = 0;
	for (i = 0, rv = 0, kp = plist; i < nproc; i++, kp++) {
		if (PSKIP(kp))
			continue;
		if (selected[i]) {
			if (longfmt && !pgrep) {
				did_action = 1;
				printf("kill -%d %d\n", signum, kp->ki_pid);
			}
			if (inverse)
				continue;
		} else if (!inverse)
			continue;
		rv |= (*action)(kp);
	}
	if (rv && pgrep && !quiet)
		putchar('\n');
	if (!did_action && !pgrep && longfmt)
		fprintf(stderr,
		    "No matching processes belonging to you were found\n");

	exit(rv ? STATUS_MATCH : STATUS_NOMATCH);
}

static void
usage(void)
{
	const char *ustr;

	if (pgrep)
		ustr = "[-LSfilnoqvx] [-d delim]";
	else
		ustr = "[-signal] [-ILfilnovx]";

	fprintf(stderr,
		"usage: %s %s [-F pidfile] [-G gid] [-M core] [-N system]\n"
		"             [-P ppid] [-U uid] [-c class] [-g pgrp] [-j jail]\n"
		"             [-s sid] [-t tty] [-u euid] pattern ...\n",
		getprogname(), ustr);

	exit(STATUS_BADUSAGE);
}

static void
show_process(const struct kinfo_proc *kp)
{
	char **argv;

	if (quiet) {
		assert(pgrep);
		return;
	}
	if ((longfmt || !pgrep) && matchargs &&
	    (argv = kvm_getargv(kd, kp, 0)) != NULL) {
		printf("%d ", (int)kp->ki_pid);
		for (; *argv != NULL; argv++) {
			printf("%s", *argv);
			if (argv[1] != NULL)
				putchar(' ');
		}
	} else if (longfmt || !pgrep)
		printf("%d %s", (int)kp->ki_pid, kp->ki_comm);
	else
		printf("%d", (int)kp->ki_pid);
}

static int
killact(const struct kinfo_proc *kp)
{
	int ch, first;

	if (interactive) {
		/*
		 * Be careful, ask before killing.
		 */
		printf("kill ");
		show_process(kp);
		printf("? ");
		fflush(stdout);
		first = ch = getchar();
		while (ch != '\n' && ch != EOF)
			ch = getchar();
		if (first != 'y' && first != 'Y')
			return (1);
	}
	if (kill(kp->ki_pid, signum) == -1) {
		/* 
		 * Check for ESRCH, which indicates that the process
		 * disappeared between us matching it and us
		 * signalling it; don't issue a warning about it.
		 */
		if (errno != ESRCH)
			warn("signalling pid %d", (int)kp->ki_pid);
		/*
		 * Return 0 to indicate that the process should not be
		 * considered a match, since we didn't actually get to
		 * signal it.
		 */
		return (0);
	}
	return (1);
}

static int
grepact(const struct kinfo_proc *kp)
{
	static bool first = true;

	if (!quiet && !first)
		printf("%s", delim);
	show_process(kp);
	first = false;
	return (1);
}

static void
makelist(struct listhead *head, enum listtype type, char *src)
{
	struct list *li;
	struct passwd *pw;
	struct group *gr;
	struct stat st;
	const char *cp;
	char *sp, *ep, buf[MAXPATHLEN];
	int empty;

	empty = 1;

	while ((sp = strsep(&src, ",")) != NULL) {
		if (*sp == '\0')
			usage();

		if ((li = malloc(sizeof(*li))) == NULL) {
			err(STATUS_ERROR, "Cannot allocate %zu bytes",
			    sizeof(*li));
		}

		SLIST_INSERT_HEAD(head, li, li_chain);
		empty = 0;

		if (type != LT_CLASS)
			li->li_number = (uid_t)strtol(sp, &ep, 0);

		if (type != LT_CLASS && *ep == '\0') {
			switch (type) {
			case LT_PGRP:
				if (li->li_number == 0)
					li->li_number = getpgrp();
				break;
			case LT_SID:
				if (li->li_number == 0)
					li->li_number = getsid(mypid);
				break;
			case LT_JAIL:
				if (li->li_number < 0)
					errx(STATUS_BADUSAGE,
					     "Negative jail ID `%s'", sp);
				/* For compatibility with old -j */
				if (li->li_number == 0)
					li->li_number = -1;	/* any jail */
				break;
			case LT_TTY:
				if (li->li_number < 0)
					errx(STATUS_BADUSAGE,
					     "Negative /dev/pts tty `%s'", sp);
				snprintf(buf, sizeof(buf), _PATH_DEV "pts/%s",
				    sp);
				if (stat(buf, &st) != -1)
					goto foundtty;
				if (errno == ENOENT)
					errx(STATUS_BADUSAGE, "No such tty: `"
					    _PATH_DEV "pts/%s'", sp);
				err(STATUS_ERROR, "Cannot access `"
				    _PATH_DEV "pts/%s'", sp);
				break;
			default:
				break;
			}
			continue;
		}

		switch (type) {
		case LT_USER:
			if ((pw = getpwnam(sp)) == NULL)
				errx(STATUS_BADUSAGE, "Unknown user `%s'", sp);
			li->li_number = pw->pw_uid;
			break;
		case LT_GROUP:
			if ((gr = getgrnam(sp)) == NULL)
				errx(STATUS_BADUSAGE, "Unknown group `%s'", sp);
			li->li_number = gr->gr_gid;
			break;
		case LT_TTY:
			if (strcmp(sp, "-") == 0) {
				li->li_number = -1;
				break;
			} else if (strcmp(sp, "co") == 0) {
				cp = "console";
			} else {
				cp = sp;
			}

			snprintf(buf, sizeof(buf), _PATH_DEV "%s", cp);
			if (stat(buf, &st) != -1)
				goto foundtty;

			snprintf(buf, sizeof(buf), _PATH_DEV "tty%s", cp);
			if (stat(buf, &st) != -1)
				goto foundtty;

			if (errno == ENOENT)
				errx(STATUS_BADUSAGE, "No such tty: `%s'", sp);
			err(STATUS_ERROR, "Cannot access `%s'", sp);

foundtty:		if ((st.st_mode & S_IFCHR) == 0)
				errx(STATUS_BADUSAGE, "Not a tty: `%s'", sp);

			li->li_number = st.st_rdev;
			break;
		case LT_JAIL: {
			int jid;

			if (strcmp(sp, "none") == 0)
				li->li_number = 0;
			else if (strcmp(sp, "any") == 0)
				li->li_number = -1;
			else if ((jid = jail_getid(sp)) != -1)
				li->li_number = jid;
			else if (*ep != '\0')
				errx(STATUS_BADUSAGE,
				     "Invalid jail ID or name `%s'", sp);
			break;
		}
		case LT_CLASS:
			li->li_number = -1;
			li->li_name = strdup(sp);
			if (li->li_name == NULL)
				err(STATUS_ERROR, "Cannot allocate memory");
			break;
		default:
			usage();
		}
	}

	if (empty)
		usage();
}

static int
takepid(const char *pidfile, int pidfilelock)
{
	char *endp, line[BUFSIZ];
	FILE *fh;
	long rval;

	fh = fopen(pidfile, "r");
	if (fh == NULL)
		err(STATUS_ERROR, "Cannot open pidfile `%s'", pidfile);

	if (pidfilelock) {
		/*
		 * If we can lock pidfile, this means that daemon is not
		 * running, so would be better not to kill some random process.
		 */
		if (flock(fileno(fh), LOCK_EX | LOCK_NB) == 0) {
			(void)fclose(fh);
			errx(STATUS_ERROR, "File '%s' can be locked", pidfile);
		} else {
			if (errno != EWOULDBLOCK) {
				errx(STATUS_ERROR,
				    "Error while locking file '%s'", pidfile);
			}
		}
	}

	if (fgets(line, sizeof(line), fh) == NULL) {
		if (feof(fh)) {
			(void)fclose(fh);
			errx(STATUS_ERROR, "Pidfile `%s' is empty", pidfile);
		}
		(void)fclose(fh);
		err(STATUS_ERROR, "Cannot read from pid file `%s'", pidfile);
	}
	(void)fclose(fh);

	rval = strtol(line, &endp, 10);
	if (*endp != '\0' && !isspace((unsigned char)*endp))
		errx(STATUS_ERROR, "Invalid pid in file `%s'", pidfile);
	else if (rval < MIN_PID || rval > MAX_PID)
		errx(STATUS_ERROR, "Invalid pid in file `%s'", pidfile);
	return (rval);
}
