/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <signal.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#define NANOSEC	1000000000

int
main(int argc, char **argv)
{
	struct sigevent ev;
	struct itimerspec ts;
	sigset_t set;
	timer_t tid;
	char *cmd = argv[0];
	int sig;

	ev.sigev_notify = SIGEV_SIGNAL;
	ev.sigev_signo = SIGUSR1;

	if (timer_create(CLOCK_REALTIME, &ev, &tid) == -1) {
		(void) fprintf(stderr, "%s: cannot create CLOCK_HIGHRES "
		    "timer: %s\n", cmd, strerror(errno));
		exit(EXIT_FAILURE);
	}

	(void) sigemptyset(&set);
	(void) sigaddset(&set, SIGUSR1);
	(void) sigprocmask(SIG_BLOCK, &set, NULL);

	ts.it_value.tv_sec = 1;
	ts.it_value.tv_nsec = 0;
	ts.it_interval.tv_sec = 0;
	ts.it_interval.tv_nsec = NANOSEC / 2;

	if (timer_settime(tid, TIMER_RELTIME, &ts, NULL) == -1) {
		(void) fprintf(stderr, "%s: timer_settime() failed: %s\n",
		    cmd, strerror(errno));
		exit(EXIT_FAILURE);
	}

	do {
		(void) sigwait(&set, &sig);
	} while(sig != SIGUSR1);

	/*NOTREACHED*/
	return (0);
}
