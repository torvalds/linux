/*	$NetBSD: t_tcsetpgrp.c,v 1.3 2012/03/18 07:14:08 jruoho Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
__RCSID("$NetBSD: t_tcsetpgrp.c,v 1.3 2012/03/18 07:14:08 jruoho Exp $");

#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

ATF_TC(tcsetpgrp_err);
ATF_TC_HEAD(tcsetpgrp_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from tcsetpgrp(3)"
	    " (PR lib/41673)");
}

ATF_TC_BODY(tcsetpgrp_err, tc)
{
	int rv, sta;
	pid_t pid;

	if (isatty(STDIN_FILENO) == 0)
		return;

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		/*
		 * The child process ID doesn't match any active
		 * process group ID, so the following call should
		 * fail with EPERM (and not EINVAL).
		 */
		errno = 0;
		rv = tcsetpgrp(STDIN_FILENO, getpid());

		if (rv == 0 || errno != EPERM)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("wrong errno");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, tcsetpgrp_err);

	return atf_no_error();
}
