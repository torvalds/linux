/*	$OpenBSD: sleep.c,v 1.29 2020/02/25 15:46:15 cheloha Exp $	*/
/*	$NetBSD: sleep.c,v 1.8 1995/03/21 09:11:11 cgd Exp $	*/

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

#include <sys/time.h>

#include <ctype.h>
#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

void alarmh(int);
void usage(void);

int
main(int argc, char *argv[])
{
	struct timespec rqtp;
	time_t t;
	char *cp;
	int ch, i;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	signal(SIGALRM, alarmh);

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch(ch) {
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	timespecclear(&rqtp);

	/* Handle whole seconds. */
	for (cp = argv[0]; *cp != '\0' && *cp != '.'; cp++) {
		if (!isdigit((unsigned char)*cp))
			errx(1, "seconds is invalid: %s", argv[0]);
		t = (rqtp.tv_sec * 10) + (*cp - '0');
		if (t / 10 != rqtp.tv_sec)	/* overflow */
			errx(1, "seconds is too large: %s", argv[0]);
		rqtp.tv_sec = t;
	}

	/*
	 * Handle fractions of a second.  The multiplier divides to zero
	 * after nine digits so anything more precise than a nanosecond is
	 * validated but not used.
	 */
	if (*cp == '.') {
		i = 100000000;
		for (cp++; *cp != '\0'; cp++) {
			if (!isdigit((unsigned char)*cp))
				errx(1, "seconds is invalid: %s", argv[0]);
			rqtp.tv_nsec += (*cp - '0') * i;
			i /= 10;
		}
	}

	if (timespecisset(&rqtp)) {
		if (nanosleep(&rqtp, NULL) == -1)
			err(1, "nanosleep");
	}

	return 0;
}

void
usage(void)
{
	fprintf(stderr, "usage: %s seconds\n", getprogname());
	exit(1);
}

/*
 * POSIX.1 says sleep(1) may exit with status zero upon receipt
 * of SIGALRM.
 */
void
alarmh(int signo)
{
	/*
	 * Always _exit(2) from signal handlers: exit(3) is not
	 * generally signal-safe.
	 */
	_exit(0);
}
