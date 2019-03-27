/* $NetBSD: t_fileactions.c,v 1.6 2017/01/10 22:36:29 christos Exp $ */

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

#include <sys/wait.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>


ATF_TC(t_spawn_openmode);

ATF_TC_HEAD(t_spawn_openmode, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test the proper handling of 'mode' for 'open' fileactions");
	atf_tc_set_md_var(tc, "require.progs", "/bin/cat");
}

static off_t
filesize(const char * restrict fname)
{
	struct stat st;
	int err;

	err = stat(fname, &st);
	ATF_REQUIRE(err == 0);
	return st.st_size;
}

#define TESTFILE	"./the_input_data"
#define CHECKFILE	"./the_output_data"
#define TESTCONTENT	"marry has a little lamb"

static void
make_testfile(const char *restrict file)
{
	FILE *f;
	size_t written;

	f = fopen(file, "w");
	ATF_REQUIRE(f != NULL);
	written = fwrite(TESTCONTENT, 1, strlen(TESTCONTENT), f);
	fclose(f);
	ATF_REQUIRE(written == strlen(TESTCONTENT));
}

static void
empty_outfile(const char *restrict filename)
{
	FILE *f;

	f = fopen(filename, "w");
	ATF_REQUIRE(f != NULL);
	fclose(f);
}

ATF_TC_BODY(t_spawn_openmode, tc)
{
	int status, err;
	pid_t pid;
	size_t insize, outsize;
	char * const args[2] = { __UNCONST("cat"), NULL };
	posix_spawn_file_actions_t fa;

	/*
	 * try a "cat < testfile > checkfile"
	 */
	make_testfile(TESTFILE);
	unlink(CHECKFILE);

	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, fileno(stdin),
	    TESTFILE, O_RDONLY, 0);
	posix_spawn_file_actions_addopen(&fa, fileno(stdout),
	    CHECKFILE, O_WRONLY|O_CREAT, 0600);
	err = posix_spawn(&pid, "/bin/cat", &fa, NULL, args, NULL);
	posix_spawn_file_actions_destroy(&fa);

	ATF_REQUIRE(err == 0);

	/* ok, wait for the child to finish */
	waitpid(pid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS);

	/* now check that input and output have the same size */
	insize = filesize(TESTFILE);
	outsize = filesize(CHECKFILE);
	ATF_REQUIRE(insize == strlen(TESTCONTENT));
	ATF_REQUIRE(insize == outsize);

	/*
	 * try a "cat < testfile >> checkfile"
	 */
	make_testfile(TESTFILE);
	make_testfile(CHECKFILE);

	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, fileno(stdin),
	    TESTFILE, O_RDONLY, 0);
	posix_spawn_file_actions_addopen(&fa, fileno(stdout),
	    CHECKFILE, O_WRONLY|O_APPEND, 0);
	err = posix_spawn(&pid, "/bin/cat", &fa, NULL, args, NULL);
	posix_spawn_file_actions_destroy(&fa);

	ATF_REQUIRE(err == 0);

	/* ok, wait for the child to finish */
	waitpid(pid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS);

	/* now check that output is twice as long as input */
	insize = filesize(TESTFILE);
	outsize = filesize(CHECKFILE);
	ATF_REQUIRE(insize == strlen(TESTCONTENT));
	ATF_REQUIRE(insize*2 == outsize);

	/*
	 * try a "cat < testfile  > checkfile" with input and output swapped
	 */
	make_testfile(TESTFILE);
	empty_outfile(CHECKFILE);

	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, fileno(stdout),
	    TESTFILE, O_RDONLY, 0);
	posix_spawn_file_actions_addopen(&fa, fileno(stdin),
	    CHECKFILE, O_WRONLY, 0);
	err = posix_spawn(&pid, "/bin/cat", &fa, NULL, args, NULL);
	posix_spawn_file_actions_destroy(&fa);

	ATF_REQUIRE(err == 0);

	/* ok, wait for the child to finish */
	waitpid(pid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE);

	/* now check that input and output are still the same size */
	insize = filesize(TESTFILE);
	outsize = filesize(CHECKFILE);
	ATF_REQUIRE(insize == strlen(TESTCONTENT));
	ATF_REQUIRE(outsize == 0);
}

ATF_TC(t_spawn_reopen);

ATF_TC_HEAD(t_spawn_reopen, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "an open filehandle can be replaced by a 'open' fileaction");
	atf_tc_set_md_var(tc, "require.progs", "/bin/cat");
}

ATF_TC_BODY(t_spawn_reopen, tc)
{
	int status, err;
	pid_t pid;
	char * const args[2] = { __UNCONST("cat"), NULL };
	posix_spawn_file_actions_t fa;

	/*
	 * make sure stdin is open in the parent
	 */
	freopen("/dev/zero", "r", stdin);
	/*
	 * now request an open for this fd again in the child
	 */
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, fileno(stdin),
	    "/dev/null", O_RDONLY, 0);
	err = posix_spawn(&pid, "/bin/cat", &fa, NULL, args, NULL);
	posix_spawn_file_actions_destroy(&fa);

	ATF_REQUIRE(err == 0);

	waitpid(pid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS);
}

ATF_TC(t_spawn_open_nonexistent);

ATF_TC_HEAD(t_spawn_open_nonexistent, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "posix_spawn fails when a file to open does not exist");
	atf_tc_set_md_var(tc, "require.progs", "/bin/cat");
}

ATF_TC_BODY(t_spawn_open_nonexistent, tc)
{
	int err, status;
	pid_t pid;
	char * const args[2] = { __UNCONST("cat"), NULL };
	posix_spawn_file_actions_t fa;

	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, STDIN_FILENO,
	    "./non/ex/ist/ent", O_RDONLY, 0);
	err = posix_spawn(&pid, "/bin/cat", &fa, NULL, args, NULL);
	if (err == 0) {
		/*
		 * The child has been created - it should fail and
		 * return exit code 127
		 */
		waitpid(pid, &status, 0);
		ATF_REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == 127);
	} else {
		/*
		 * The error has been noticed early enough, no child has
		 * been run
		 */
		ATF_REQUIRE(err == ENOENT);
	}
	posix_spawn_file_actions_destroy(&fa);
}

#ifdef __NetBSD__
ATF_TC(t_spawn_open_nonexistent_diag);

ATF_TC_HEAD(t_spawn_open_nonexistent_diag, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "posix_spawn fails when a file to open does not exist "
	    "and delivers proper diagnostic");
	atf_tc_set_md_var(tc, "require.progs", "/bin/cat");
}

ATF_TC_BODY(t_spawn_open_nonexistent_diag, tc)
{
	int err;
	pid_t pid;
	char * const args[2] = { __UNCONST("cat"), NULL };
	posix_spawnattr_t attr;
	posix_spawn_file_actions_t fa;

	posix_spawnattr_init(&attr);
	/*
	 * POSIX_SPAWN_RETURNERROR is a NetBSD specific flag that
	 * will cause a "proper" return value from posix_spawn(2)
	 * instead of a (potential) success there and a 127 exit
	 * status from the child process (c.f. the non-diag variant
	 * of this test).
	 */
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_RETURNERROR);
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, STDIN_FILENO,
	    "./non/ex/ist/ent", O_RDONLY, 0);
	err = posix_spawn(&pid, "/bin/cat", &fa, &attr, args, NULL);
	ATF_REQUIRE(err == ENOENT);
	posix_spawn_file_actions_destroy(&fa);
	posix_spawnattr_destroy(&attr);
}
#endif

ATF_TC(t_spawn_fileactions);

ATF_TC_HEAD(t_spawn_fileactions, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Tests various complex fileactions");
}

ATF_TC_BODY(t_spawn_fileactions, tc)
{
	int fd1, fd2, fd3, status, err;
	pid_t pid;
	char * const args[2] = { __UNCONST("h_fileactions"), NULL };
	char helper[FILENAME_MAX];
	posix_spawn_file_actions_t fa;

	posix_spawn_file_actions_init(&fa);

	closefrom(fileno(stderr)+1);

	fd1 = open("/dev/null", O_RDONLY);
	ATF_REQUIRE(fd1 == 3);

	fd2 = open("/dev/null", O_WRONLY, O_CLOEXEC);
	ATF_REQUIRE(fd2 == 4);

	fd3 = open("/dev/null", O_WRONLY);
	ATF_REQUIRE(fd3 == 5);

	posix_spawn_file_actions_addclose(&fa, fd1);
	posix_spawn_file_actions_addopen(&fa, 6, "/dev/null", O_RDWR, 0);
	posix_spawn_file_actions_adddup2(&fa, 1, 7); 

	snprintf(helper, sizeof helper, "%s/h_fileactions",
	    atf_tc_get_config_var(tc, "srcdir"));
	err = posix_spawn(&pid, helper, &fa, NULL, args, NULL);
	posix_spawn_file_actions_destroy(&fa);

	ATF_REQUIRE(err == 0);

	waitpid(pid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS);
}

ATF_TC(t_spawn_empty_fileactions);

ATF_TC_HEAD(t_spawn_empty_fileactions, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "posix_spawn with empty fileactions (PR kern/46038)");
	atf_tc_set_md_var(tc, "require.progs", "/bin/cat");
}

ATF_TC_BODY(t_spawn_empty_fileactions, tc)
{
	int status, err;
	pid_t pid;
	char * const args[2] = { __UNCONST("cat"), NULL };
	posix_spawn_file_actions_t fa;
	size_t insize, outsize;

	/*
	 * try a "cat < testfile > checkfile", but set up stdin/stdout
	 * already in the parent and pass empty file actions to the child.
	 */
	make_testfile(TESTFILE);
	unlink(CHECKFILE);

	freopen(TESTFILE, "r", stdin);
	freopen(CHECKFILE, "w", stdout);

	posix_spawn_file_actions_init(&fa);
	err = posix_spawn(&pid, "/bin/cat", &fa, NULL, args, NULL);
	posix_spawn_file_actions_destroy(&fa);

	ATF_REQUIRE(err == 0);

	/* ok, wait for the child to finish */
	waitpid(pid, &status, 0);
	ATF_REQUIRE(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS);

	/* now check that input and output have the same size */
	insize = filesize(TESTFILE);
	outsize = filesize(CHECKFILE);
	ATF_REQUIRE(insize == strlen(TESTCONTENT));
	ATF_REQUIRE(insize == outsize);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, t_spawn_fileactions);
	ATF_TP_ADD_TC(tp, t_spawn_open_nonexistent);
#ifdef __NetBSD__
	ATF_TP_ADD_TC(tp, t_spawn_open_nonexistent_diag);
#endif
	ATF_TP_ADD_TC(tp, t_spawn_reopen);
	ATF_TP_ADD_TC(tp, t_spawn_openmode);
	ATF_TP_ADD_TC(tp, t_spawn_empty_fileactions);

	return atf_no_error();
}
