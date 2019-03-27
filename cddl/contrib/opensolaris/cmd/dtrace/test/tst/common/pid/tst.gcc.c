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

#include <sys/types.h>
#include <sys/wait.h>
#include <spawn.h>
#include <signal.h>
#include <stdio.h>

void
go(void)
{
	pid_t pid;

	(void) posix_spawn(&pid, "/bin/ls", NULL, NULL, NULL, NULL);

	(void) waitpid(pid, NULL, 0);
}

void
intr(int sig)
{
}

int
main(int argc, char **argv)
{
	struct sigaction sa;

	sa.sa_handler = intr;
	sigfillset(&sa.sa_mask);
	sa.sa_flags = 0;

	(void) sigaction(SIGUSR1, &sa, NULL);

	for (;;) {
		go();
	}

	return (0);
}
