/*	$OpenBSD: kill.c,v 1.15 2025/04/24 14:15:29 schwarze Exp $	*/
/*	$NetBSD: kill.c,v 1.11 1995/09/07 06:30:27 jtc Exp $	*/

/*
 * Copyright (c) 1988, 1993, 1994
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

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern	char *__progname;

void nosig(char *);
void printsignals(FILE *);
int signame_to_signum(char *);
void usage(void);

int
main(int argc, char *argv[])
{
	int errors, numsig, pid;
	const char *errstr;

	if (pledge("stdio proc", NULL) == -1)
		err(1, "pledge");

	if (argc < 2)
		usage();

	numsig = SIGTERM;

	argc--, argv++;
	if (!strcmp(*argv, "-l")) {
		argc--, argv++;
		if (argc > 0 && !strcmp(*argv, "--"))
			argc--, argv++;
		if (argc > 1)
			usage();
		if (argc == 1) {
			if (!isdigit((unsigned char)**argv))
				usage();
			numsig = strtonum(*argv, 1, NSIG + 127, &errstr);
			if (errstr != NULL) {
				if (errno == ERANGE)
					nosig(*argv);
				errx(1, "illegal signal number: %s", *argv);
			}
			printf("%s\n", sys_signame[numsig & 127]);
			exit(0);
		}
		printsignals(stdout);
		exit(0);
	}

	if (!strcmp(*argv, "-s")) {
		argc--, argv++;
		if (argc > 0 && !strcmp(*argv, "--"))
			argc--, argv++;
		if (argc < 1) {
			warnx("option requires an argument -- s");
			usage();
		}
		if (strcmp(*argv, "0")) {
			if ((numsig = signame_to_signum(*argv)) < 0)
				nosig(*argv);
		} else
			numsig = 0;
		argc--, argv++;
	} else if (**argv == '-') {
		if (strcmp(*argv, "--")) {
			++*argv;
			if (isalpha((unsigned char)**argv)) {
				if ((numsig = signame_to_signum(*argv)) < 0)
					nosig(*argv);
			} else if (isdigit((unsigned char)**argv)) {
				numsig = strtonum(*argv, 0, NSIG - 1, &errstr);
				if (errstr != NULL) {
					if (errno == ERANGE)
						nosig(*argv);
					errx(1, "illegal signal number: %s", *argv);
				}
			} else
				nosig(*argv);
		}
		argc--, argv++;
	}

	if (argc == 0)
		usage();

	for (errors = 0; argc; argc--, argv++) {
		pid = strtonum(*argv, -INT_MAX, INT_MAX, &errstr);
		if (errstr != NULL) {
			warnx("illegal process id: %s", *argv);
			errors = 1;
		} else if (kill(pid, numsig) == -1) {
			warn("%s", *argv);
			errors = 1;
		}
	}

	exit(errors);
}

int
signame_to_signum(char *sig)
{
	int n;

	if (!strncasecmp(sig, "sig", 3))
		sig += 3;
	for (n = 1; n < NSIG; n++) {
		if (!strcasecmp(sys_signame[n], sig))
			return (n);
	}
	return (-1);
}

void
nosig(char *name)
{

	warnx("unknown signal %s; valid signals:", name);
	printsignals(stderr);
	exit(1);
}

void
printsignals(FILE *fp)
{
	int n;

	for (n = 1; n < NSIG; n++) {
		(void)fprintf(fp, "%s", sys_signame[n]);
		if (n == (NSIG / 2) || n == (NSIG - 1))
			(void)fprintf(fp, "\n");
		else
			(void)fprintf(fp, " ");
	}
}

void
usage(void)
{
	(void)fprintf(stderr, "usage: %s [-signal_number | -signal_name |"
	    " -s signal_name] pid ...\n", __progname);
	(void)fprintf(stderr, "       %s -l [exit_status]\n", __progname);
	exit(1);
}
