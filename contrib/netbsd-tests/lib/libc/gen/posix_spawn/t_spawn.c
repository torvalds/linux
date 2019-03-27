/* $NetBSD: t_spawn.c,v 1.2 2014/10/18 08:33:30 snj Exp $ */

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


#include <atf-c.h>
#include <spawn.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/wait.h>

ATF_TC(t_spawn_ls);

ATF_TC_HEAD(t_spawn_ls, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Tests a simple posix_spawn executing /bin/ls");
}

ATF_TC_BODY(t_spawn_ls, tc)
{
	char * const args[] = { __UNCONST("ls"), __UNCONST("-la"), NULL };
	int err;

	err = posix_spawn(NULL, "/bin/ls", NULL, NULL, args, NULL);
	ATF_REQUIRE(err == 0);
}

ATF_TC(t_spawnp_ls);

ATF_TC_HEAD(t_spawnp_ls, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Tests a simple posix_spawnp executing ls via $PATH");
}

ATF_TC_BODY(t_spawnp_ls, tc)
{
	char * const args[] = { __UNCONST("ls"), __UNCONST("-la"), NULL };
	int err;

	err = posix_spawnp(NULL, "ls", NULL, NULL, args, NULL);
	ATF_REQUIRE(err == 0);
}

ATF_TC(t_spawn_zero);

ATF_TC_HEAD(t_spawn_zero, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "posix_spawn an invalid binary");
}

ATF_TC_BODY(t_spawn_zero, tc)
{
	char buf[FILENAME_MAX];
	char * const args[] = { __UNCONST("h_zero"), NULL };
	int err;

	snprintf(buf, sizeof buf, "%s/h_zero", atf_tc_get_config_var(tc, "srcdir"));
	err = posix_spawn(NULL, buf, NULL, NULL, args, NULL);
	ATF_REQUIRE_MSG(err == ENOEXEC, "expected error %d, got %d when spawning %s", ENOEXEC, err, buf);
}

ATF_TC(t_spawn_missing);

ATF_TC_HEAD(t_spawn_missing, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "posix_spawn a non existant binary");
}

ATF_TC_BODY(t_spawn_missing, tc)
{
	char buf[FILENAME_MAX];
	char * const args[] = { __UNCONST("h_nonexist"), NULL };
	int err;

	snprintf(buf, sizeof buf, "%s/h_nonexist",
	    atf_tc_get_config_var(tc, "srcdir"));
	err = posix_spawn(NULL, buf, NULL, NULL, args, NULL);
	ATF_REQUIRE_MSG(err == ENOENT, "expected error %d, got %d when spawning %s", ENOENT, err, buf);
}

ATF_TC(t_spawn_nonexec);

ATF_TC_HEAD(t_spawn_nonexec, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "posix_spawn a script with non existing interpreter");
}

ATF_TC_BODY(t_spawn_nonexec, tc)
{
	char buf[FILENAME_MAX];
	char * const args[] = { __UNCONST("h_nonexec"), NULL };
	int err;

	snprintf(buf, sizeof buf, "%s/h_nonexec",
	    atf_tc_get_config_var(tc, "srcdir"));
	err = posix_spawn(NULL, buf, NULL, NULL, args, NULL);
	ATF_REQUIRE_MSG(err == ENOENT, "expected error %d, got %d when spawning %s", ENOENT, err, buf);
}

ATF_TC(t_spawn_child);

ATF_TC_HEAD(t_spawn_child, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "posix_spawn a child and get its return code");
}

ATF_TC_BODY(t_spawn_child, tc)
{
	char buf[FILENAME_MAX];
	char * const args0[] = { __UNCONST("h_spawn"), __UNCONST("0"), NULL };
	char * const args1[] = { __UNCONST("h_spawn"), __UNCONST("1"), NULL };
	char * const args7[] = { __UNCONST("h_spawn"), __UNCONST("7"), NULL };
	int err, status;
	pid_t pid;

	snprintf(buf, sizeof buf, "%s/h_spawn",
	    atf_tc_get_config_var(tc, "srcdir"));

	err = posix_spawn(&pid, buf, NULL, NULL, args0, NULL);
	ATF_REQUIRE(err == 0);
	ATF_REQUIRE(pid > 0);
	waitpid(pid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	err = posix_spawn(&pid, buf, NULL, NULL, args1, NULL);
	ATF_REQUIRE(err == 0);
	ATF_REQUIRE(pid > 0);
	waitpid(pid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 1);

	err = posix_spawn(&pid, buf, NULL, NULL, args7, NULL);
	ATF_REQUIRE(err == 0);
	ATF_REQUIRE(pid > 0);
	waitpid(pid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 7);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, t_spawn_ls);
	ATF_TP_ADD_TC(tp, t_spawnp_ls);
	ATF_TP_ADD_TC(tp, t_spawn_zero);
	ATF_TP_ADD_TC(tp, t_spawn_missing);
	ATF_TP_ADD_TC(tp, t_spawn_nonexec);
	ATF_TP_ADD_TC(tp, t_spawn_child);

	return atf_no_error();
}
