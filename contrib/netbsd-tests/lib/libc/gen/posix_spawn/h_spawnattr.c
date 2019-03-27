/* $NetBSD: h_spawnattr.c,v 1.1 2012/02/13 21:03:08 martin Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles Zhang <charles@NetBSD.org> and
 * Martin Husemann <martin@NetBSD.org>.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

/*
 * Helper to test the hardcoded assumptions from t_spawnattr.c
 * Exit with apropriate exit status and print diagnostics to
 * stderr explaining what is wrong.
 */
int
main(int argc, char **argv)
{
	int parent_pipe, res = EXIT_SUCCESS;
	sigset_t sig;
	struct sigaction act;
	ssize_t rd;
	char tmp;

	sigemptyset(&sig);
	if (sigprocmask(0, NULL, &sig) < 0) {
		fprintf(stderr, "%s: sigprocmask error\n", getprogname());
		res = EXIT_FAILURE;
	}
	if (!sigismember(&sig, SIGUSR1)) {
		fprintf(stderr, "%s: SIGUSR not in procmask\n", getprogname());
		res = EXIT_FAILURE;
	}
	if (sigaction(SIGUSR1, NULL, &act) < 0) {
		fprintf(stderr, "%s: sigaction error\n", getprogname());
		res = EXIT_FAILURE;
	}
	if (act.sa_sigaction != (void *)SIG_DFL) {
		fprintf(stderr, "%s: SIGUSR1 action != SIG_DFL\n",
		    getprogname());
		res = EXIT_FAILURE;
	}

	if (argc >= 2) {
		parent_pipe = atoi(argv[1]);
		if (parent_pipe > 2) {
			printf("%s: waiting for command from parent on pipe "
			    "%d\n", getprogname(), parent_pipe);
			rd = read(parent_pipe, &tmp, 1);
			if (rd == 1) {
				printf("%s: got command %c from parent\n",
				    getprogname(), tmp);
			} else if (rd == -1) {
				printf("%s: %d is no pipe, errno %d\n",
				    getprogname(), parent_pipe, errno);
				res = EXIT_FAILURE;
			}
		}
	}

	return res;
}
