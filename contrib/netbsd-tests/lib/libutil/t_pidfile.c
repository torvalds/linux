/* $NetBSD: t_pidfile.c,v 1.3 2011/03/29 13:55:37 jmmv Exp $ */

/*
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
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

/*
 * This file implements tests for the pidfile(3) functions.
 *
 * The tests here are tricky because we need to validate that the atexit(3)
 * handler registered by pidfile(3) correctly deletes the generated pidfile.
 * To do so:
 * 1) We spawn a subprocess in every test case,
 * 2) Run our test code in such subprocesses.  We cannot call any of the ATF
 *    primitives from inside these.
 * 3) Wait for the subprocess to terminate and ensure it exited successfully.
 * 4) Check that the pidfile(s) created in the subprocess are gone.
 *
 * Additionally, pidfile(3) hardcodes a path to a directory writable only by
 * root (/var/run).  This makes us require root privileges to execute these
 * tests.
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2011\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_pidfile.c,v 1.3 2011/03/29 13:55:37 jmmv Exp $");

#include <sys/stat.h>
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <paths.h>
#include <unistd.h>
#include <util.h>

#include <atf-c.h>

/* Used by routines that can be called both from the parent and the child
 * code to implement proper error reporting. */
static bool in_child = false;

/* Callable from the test case child code. */
static void
check_pidfile(const char *path)
{
	FILE *file;
	int pid;

	printf("Validating contents of pidfile '%s'\n", path);

	if ((file = fopen(path, "r")) == NULL)
		errx(EXIT_FAILURE, "Cannot open expected pidfile '%s'", path);

	if (fscanf(file, "%d", &pid) == -1)
		errx(EXIT_FAILURE, "Failed to read pid from pidfile '%s'",
		    path);

	printf("Read pid %d, current pid %d\n", pid, getpid());
	if (pid != getpid())
		errx(EXIT_FAILURE, "Pid in pidfile (%d) does not match "
		    "current pid (%d)", pid, getpid());
}

/* Callable from the test case parent/child code. */
static void
ensure_deleted(const char *path)
{
	printf("Ensuring pidfile %s does not exist any more\n", path);
	if (access(path, R_OK) != -1) {
		unlink(path);
		if (in_child)
			errx(EXIT_FAILURE, "The pidfile %s was not deleted",
			    path);
		else
			atf_tc_fail("The pidfile %s was not deleted", path);
	}
}

/* Callable from the test case parent code. */
static void
run_child(void (*child)(const char *), const char *cookie)
{
	pid_t pid;

	pid = fork();
	ATF_REQUIRE(pid != -1);
	if (pid == 0) {
		in_child = true;
		child(cookie);
		assert(false);
		/* UNREACHABLE */
	} else {
		int status;

		ATF_REQUIRE(waitpid(pid, &status, 0) != -1);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != EXIT_SUCCESS)
			atf_tc_fail("See stderr for details");
	}
}

/* Callable from the test case parent/child code. */
static char *
generate_varrun_pidfile(const char *basename)
{
	char *path;

	if (asprintf(&path, "%s%s.pid", _PATH_VARRUN,
	    basename == NULL ? getprogname() : basename) == -1) {
		if (in_child)
			errx(EXIT_FAILURE, "Cannot allocate memory for path");
		else
			atf_tc_fail("Cannot allocate memory for path");
	}

	return path;
}

static void
helper_default_path(const char *path)
{

	if (pidfile(NULL) == -1)
		errx(EXIT_FAILURE, "Failed to create pidfile with default "
		    "basename");

	check_pidfile(path);
	exit(EXIT_SUCCESS);
}

ATF_TC(default_path);
ATF_TC_HEAD(default_path, tc)
{
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(default_path, tc)
{
	char *path;

	path = generate_varrun_pidfile(NULL);
	run_child(helper_default_path, path);
	ensure_deleted(path);
	free(path);
}

static void
helper_custom_basename(const char *path)
{

	if (pidfile("custom-basename") == -1)
		errx(EXIT_FAILURE, "Failed to create pidfile with custom "
		    "basename");

	check_pidfile(path);
	exit(EXIT_SUCCESS);
}

ATF_TC(custom_basename);
ATF_TC_HEAD(custom_basename, tc)
{
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(custom_basename, tc)
{
	char *path;

	path = generate_varrun_pidfile("custom-basename");
	run_child(helper_custom_basename, path);
	ensure_deleted(path);
	free(path);
}

static void
helper_custom_path(const char *path)
{

	if (pidfile(path) == -1)
		errx(EXIT_FAILURE, "Failed to create pidfile '%s'", path);
	check_pidfile(path);
	exit(EXIT_SUCCESS);
}

ATF_TC_WITHOUT_HEAD(custom_path);
ATF_TC_BODY(custom_path, tc)
{

	ATF_REQUIRE(mkdir("var", 0777) != -1);
	ATF_REQUIRE(mkdir("var/run", 0777) != -1);

	run_child(helper_custom_path, "./var/run/my-pidfile.pid");

	ensure_deleted("./var/run/my-pidfile.pid");
}

static void
helper_change_basenames(const char *unused_cookie)
{
	char *default_path;
	char *custom_path;

	default_path = generate_varrun_pidfile(NULL);
	if (pidfile(NULL) == -1)
		errx(EXIT_FAILURE, "Failed to create pidfile with default "
		    "basename");
	check_pidfile(default_path);
	if (pidfile(NULL) == -1)
		errx(EXIT_FAILURE, "Failed to recreate pidfile with default "
		    "basename");
	check_pidfile(default_path);

	custom_path = generate_varrun_pidfile("custom-basename");
	if (pidfile("custom-basename") == -1)
		errx(EXIT_FAILURE, "Failed to create pidfile with custom "
		    "basename");
	ensure_deleted(default_path);
	check_pidfile(custom_path);
	if (pidfile("custom-basename") == -1)
		errx(EXIT_FAILURE, "Failed to recreate pidfile with custom "
		    "basename");
	check_pidfile(custom_path);

	free(custom_path);
	free(default_path);
	exit(EXIT_SUCCESS);
}

ATF_TC(change_basenames);
ATF_TC_HEAD(change_basenames, tc)
{
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(change_basenames, tc)
{
	char *default_path;
	char *custom_path;

	run_child(helper_change_basenames, NULL);

	default_path = generate_varrun_pidfile(NULL);
	custom_path = generate_varrun_pidfile("custom-basename");

	ensure_deleted(default_path);
	ensure_deleted(custom_path);

	free(custom_path);
	free(default_path);
}

static void
helper_change_paths(const char *unused_cookie)
{

	if (pidfile("./var/run/first.pid") == -1)
		errx(EXIT_FAILURE, "Failed to create pidfile "
		    "'./var/run/first.pid'");
	check_pidfile("./var/run/first.pid");

	if (pidfile("./second.pid") == -1)
		errx(EXIT_FAILURE, "Failed to create pidfile 'second.pid'");
	ensure_deleted("./var/run/first.pid");
	check_pidfile("./second.pid");

	exit(EXIT_SUCCESS);
}

ATF_TC_WITHOUT_HEAD(change_paths);
ATF_TC_BODY(change_paths, tc)
{

	ATF_REQUIRE(mkdir("var", 0777) != -1);
	ATF_REQUIRE(mkdir("var/run", 0777) != -1);

	run_child(helper_change_paths, NULL);

	ensure_deleted("./var/run/my-pidfile.pid");
	ensure_deleted("second.pid");
}

static void
helper_mix(const char *unused_cookie)
{
	char *default_path;
	char *custom_path;

	default_path = generate_varrun_pidfile(NULL);
	custom_path = generate_varrun_pidfile("custom-basename");

	if (pidfile(NULL) == -1)
		errx(EXIT_FAILURE, "Failed to create default pidfile");
	check_pidfile(default_path);

	if (pidfile("./second.pid") == -1)
		errx(EXIT_FAILURE, "Failed to create pidfile 'second.pid'");
	ensure_deleted(default_path);
	check_pidfile("./second.pid");

	if (pidfile("custom-basename") == -1)
		errx(EXIT_FAILURE, "Failed to create pidfile 'second.pid'");
	ensure_deleted(default_path);
	ensure_deleted("./second.pid");
	ensure_deleted("./custom-basename");
	check_pidfile(custom_path);

	free(custom_path);
	free(default_path);
	exit(EXIT_SUCCESS);
}

ATF_TC(change_mix);
ATF_TC_HEAD(change_mix, tc)
{
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(change_mix, tc)
{
	char *default_path;

	run_child(helper_mix, NULL);

	default_path = generate_varrun_pidfile(NULL);
	ensure_deleted(default_path);
	ensure_deleted("second.pid");
	free(default_path);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, default_path);
	ATF_TP_ADD_TC(tp, custom_basename);
	ATF_TP_ADD_TC(tp, custom_path);
	ATF_TP_ADD_TC(tp, change_basenames);
	ATF_TP_ADD_TC(tp, change_paths);
	ATF_TP_ADD_TC(tp, change_mix);

	return atf_no_error();
}
