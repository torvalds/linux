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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/ioctl.h>

#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>

static sigjmp_buf env;

static void
interrupt(int sig)
{
	siglongjmp(env, sig);
}

int
main(int argc, char *argv[])
{
	const char *file = "/dev/null";
	int i, n, fds[10];
	struct sigaction act;

	if (argc > 1) {
		(void) fprintf(stderr, "Usage: %s\n", argv[0]);
		return (EXIT_FAILURE);
	}

	act.sa_handler = interrupt;
	act.sa_flags = 0;

	(void) sigemptyset(&act.sa_mask);
	(void) sigaction(SIGUSR1, &act, NULL);

	closefrom(0);
	n = 0;

	/*
	 * With all of our file descriptors closed, wait here spinning in bogus
	 * ioctl() calls until DTrace hits us with a SIGUSR1 to start the test.
	 */
	if (sigsetjmp(env, 1) == 0) {
		for (;;)
			(void) ioctl(-1, 0, NULL);
	}

	/*
	 * To test the fds[] array, we open /dev/null (a file with reliable
	 * pathname and properties) using various flags and seek offsets.
	 */
	fds[n++] = open(file, O_RDONLY);
	fds[n++] = open(file, O_WRONLY);
	fds[n++] = open(file, O_RDWR);

	fds[n++] = open(file, O_RDWR | O_APPEND | O_CREAT |
	    O_NOCTTY | O_NONBLOCK | O_NDELAY | O_SYNC | O_TRUNC | 0666);

	fds[n++] = open(file, O_RDWR);
	(void) lseek(fds[n - 1], 123, SEEK_SET);

	/*
	 * Once we have all the file descriptors in the state we want to test,
	 * issue a bogus ioctl() on each fd with cmd 0 and arg NULL to whack
	 * our DTrace script into recording the content of the fds[] array.
	 */
	for (i = 0; i < n; i++)
		(void) ioctl(fds[i], 0, NULL);

	assert(n <= sizeof (fds) / sizeof (fds[0]));
	exit(0);
}
