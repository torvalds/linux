/* $NetBSD: h_fileactions.c,v 1.1 2012/02/13 21:03:08 martin Exp $ */

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#define BUFSIZE	16

/*
 * This checks (hardcoded) the assumptions that are setup from the
 * main test program via posix spawn file actions.
 * Program exits with EXIT_SUCCESS or EXIT_FAILURE accordingly
 * (and does some stderr diagnostics in case of errors).
 */
int
main(int argc, char **argv)
{
	int res = EXIT_SUCCESS;
	char buf[BUFSIZE];
	struct stat sb0, sb1;

	strcpy(buf, "test...");
	/* file desc 3 should be closed via addclose */
	if (read(3, buf, BUFSIZE) != -1 || errno != EBADF) {
		fprintf(stderr, "%s: filedesc 3 is not closed\n",
		    getprogname());
		res = EXIT_FAILURE;
	}
	/* file desc 4 should be closed via closeonexec */
	if (read(4, buf, BUFSIZE) != -1 || errno != EBADF) {
		fprintf(stderr, "%s: filedesc 4 is not closed\n",
		    getprogname());
		res = EXIT_FAILURE;
	}
	/* file desc 5 remains open */
	if (write(5, buf, BUFSIZE) <= 0) {
		fprintf(stderr, "%s: could not write to filedesc 5\n",
		    getprogname());
		res = EXIT_FAILURE;
	}
	/* file desc 6 should be open (via addopen) */
	if (write(6, buf, BUFSIZE) <= 0) {
		fprintf(stderr, "%s: could not write to filedesc 6\n",
		    getprogname());
		res = EXIT_FAILURE;
	}
	/* file desc 7 should refer to stdout */
	fflush(stdout);
	if (fstat(fileno(stdout), &sb0) != 0) {
		fprintf(stderr, "%s: could not fstat stdout\n",
		    getprogname());
		res = EXIT_FAILURE;
	}
	if (fstat(7, &sb1) != 0) {
		fprintf(stderr, "%s: could not fstat filedesc 7\n",
		    getprogname());
		res = EXIT_FAILURE;
	}
	if (write(7, buf, strlen(buf)) <= 0) {
		fprintf(stderr, "%s: could not write to filedesc 7\n",
		    getprogname());
		res = EXIT_FAILURE;
	}
	if (memcmp(&sb0, &sb1, sizeof sb0) != 0) {
		fprintf(stderr, "%s: stat results differ\n", getprogname());
		res = EXIT_FAILURE;
	}

	return res;
}

