/*	$NetBSD: t_fexecve.c,v 1.3 2017/01/10 15:15:09 christos Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Emmanuel Dreyfus.
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
__RCSID("$NetBSD: t_fexecve.c,v 1.3 2017/01/10 15:15:09 christos Exp $");

#include <sys/wait.h>

#include <atf-c.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>

ATF_TC(fexecve);
ATF_TC_HEAD(fexecve, tc)
{
	atf_tc_set_md_var(tc, "descr", "See that fexecve works");
}
ATF_TC_BODY(fexecve, tc)
{
	int status;
	pid_t pid;
	const char *const argv[] = { "touch", "test", NULL };
	const char *const envp[] = { NULL };

	ATF_REQUIRE((pid = fork()) != -1);
	if (pid == 0) {
		int fd;

		if ((fd = open("/usr/bin/touch", O_RDONLY, 0)) == -1)
			err(EXIT_FAILURE, "open /usr/bin/touch");

		if (fexecve(fd, __UNCONST(argv), __UNCONST(envp)) == -1) {
			int error;
			if (errno == ENOSYS)
				error = 76;
			else
				error = EXIT_FAILURE;
			(void)close(fd);
			err(error, "fexecve");
		}
	}

	ATF_REQUIRE(waitpid(pid, &status, 0) != -1);
	if (!WIFEXITED(status))
		atf_tc_fail("child process did not exit cleanly");
	if (WEXITSTATUS(status) == 76)
		atf_tc_expect_fail("fexecve not implemented");
	else
		ATF_REQUIRE(WEXITSTATUS(status) == EXIT_SUCCESS);

	ATF_REQUIRE(access("test", F_OK) == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, fexecve);

	return atf_no_error();
}
