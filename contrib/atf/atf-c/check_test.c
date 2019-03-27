/* Copyright (c) 2008 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  */

#include "atf-c/check.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "atf-c/detail/fs.h"
#include "atf-c/detail/map.h"
#include "atf-c/detail/process.h"
#include "atf-c/detail/test_helpers.h"

/* ---------------------------------------------------------------------
 * Auxiliary functions.
 * --------------------------------------------------------------------- */

static
void
do_exec(const atf_tc_t *tc, const char *helper_name, atf_check_result_t *r)
{
    atf_fs_path_t process_helpers;
    const char *argv[3];

    get_process_helpers_path(tc, false, &process_helpers);

    argv[0] = atf_fs_path_cstring(&process_helpers);
    argv[1] = helper_name;
    argv[2] = NULL;
    printf("Executing %s %s\n", argv[0], argv[1]);
    RE(atf_check_exec_array(argv, r));

    atf_fs_path_fini(&process_helpers);
}

static
void
do_exec_with_arg(const atf_tc_t *tc, const char *helper_name, const char *arg,
                 atf_check_result_t *r)
{
    atf_fs_path_t process_helpers;
    const char *argv[4];

    get_process_helpers_path(tc, false, &process_helpers);

    argv[0] = atf_fs_path_cstring(&process_helpers);
    argv[1] = helper_name;
    argv[2] = arg;
    argv[3] = NULL;
    printf("Executing %s %s %s\n", argv[0], argv[1], argv[2]);
    RE(atf_check_exec_array(argv, r));

    atf_fs_path_fini(&process_helpers);
}

static
void
check_line(int fd, const char *exp)
{
    char *line = atf_utils_readline(fd);
    ATF_CHECK(line != NULL);
    ATF_CHECK_STREQ_MSG(exp, line, "read: '%s', expected: '%s'", line, exp);
    free(line);
}

/* ---------------------------------------------------------------------
 * Helper test cases for the free functions.
 * --------------------------------------------------------------------- */

ATF_TC(h_build_c_o_ok);
ATF_TC_HEAD(h_build_c_o_ok, tc)
{
    atf_tc_set_md_var(tc, "descr", "Helper test case for build_c_o");
}
ATF_TC_BODY(h_build_c_o_ok, tc)
{
    FILE *sfile;
    bool success;

    ATF_REQUIRE((sfile = fopen("test.c", "w")) != NULL);
    fprintf(sfile, "#include <stdio.h>\n");
    fclose(sfile);

    RE(atf_check_build_c_o("test.c", "test.o", NULL, &success));
    ATF_REQUIRE(success);
}

ATF_TC(h_build_c_o_fail);
ATF_TC_HEAD(h_build_c_o_fail, tc)
{
    atf_tc_set_md_var(tc, "descr", "Helper test case for build_c_o");
}
ATF_TC_BODY(h_build_c_o_fail, tc)
{
    FILE *sfile;
    bool success;

    ATF_REQUIRE((sfile = fopen("test.c", "w")) != NULL);
    fprintf(sfile, "void foo(void) { int a = UNDEFINED_SYMBOL; }\n");
    fclose(sfile);

    RE(atf_check_build_c_o("test.c", "test.o", NULL, &success));
    ATF_REQUIRE(!success);
}

ATF_TC(h_build_cpp_ok);
ATF_TC_HEAD(h_build_cpp_ok, tc)
{
    atf_tc_set_md_var(tc, "descr", "Helper test case for build_cpp");
}
ATF_TC_BODY(h_build_cpp_ok, tc)
{
    FILE *sfile;
    bool success;
    atf_fs_path_t test_p;

    RE(atf_fs_path_init_fmt(&test_p, "test.p"));

    ATF_REQUIRE((sfile = fopen("test.c", "w")) != NULL);
    fprintf(sfile, "#define A foo\n");
    fprintf(sfile, "#define B bar\n");
    fprintf(sfile, "A B\n");
    fclose(sfile);

    RE(atf_check_build_cpp("test.c", atf_fs_path_cstring(&test_p), NULL,
                           &success));
    ATF_REQUIRE(success);

    atf_fs_path_fini(&test_p);
}

ATF_TC(h_build_cpp_fail);
ATF_TC_HEAD(h_build_cpp_fail, tc)
{
    atf_tc_set_md_var(tc, "descr", "Helper test case for build_cpp");
}
ATF_TC_BODY(h_build_cpp_fail, tc)
{
    FILE *sfile;
    bool success;

    ATF_REQUIRE((sfile = fopen("test.c", "w")) != NULL);
    fprintf(sfile, "#include \"./non-existent.h\"\n");
    fclose(sfile);

    RE(atf_check_build_cpp("test.c", "test.p", NULL, &success));
    ATF_REQUIRE(!success);
}

ATF_TC(h_build_cxx_o_ok);
ATF_TC_HEAD(h_build_cxx_o_ok, tc)
{
    atf_tc_set_md_var(tc, "descr", "Helper test case for build_cxx_o");
}
ATF_TC_BODY(h_build_cxx_o_ok, tc)
{
    FILE *sfile;
    bool success;

    ATF_REQUIRE((sfile = fopen("test.cpp", "w")) != NULL);
    fprintf(sfile, "#include <iostream>\n");
    fclose(sfile);

    RE(atf_check_build_cxx_o("test.cpp", "test.o", NULL, &success));
    ATF_REQUIRE(success);
}

ATF_TC(h_build_cxx_o_fail);
ATF_TC_HEAD(h_build_cxx_o_fail, tc)
{
    atf_tc_set_md_var(tc, "descr", "Helper test case for build_cxx_o");
}
ATF_TC_BODY(h_build_cxx_o_fail, tc)
{
    FILE *sfile;
    bool success;

    ATF_REQUIRE((sfile = fopen("test.cpp", "w")) != NULL);
    fprintf(sfile, "void foo(void) { int a = UNDEFINED_SYMBOL; }\n");
    fclose(sfile);

    RE(atf_check_build_cxx_o("test.cpp", "test.o", NULL, &success));
    ATF_REQUIRE(!success);
}

/* ---------------------------------------------------------------------
 * Test cases for the free functions.
 * --------------------------------------------------------------------- */

static
void
init_and_run_h_tc(atf_tc_t *tc, const atf_tc_pack_t *tcpack,
                  const char *outname, const char *errname)
{
    const char *const config[] = { NULL };

    RE(atf_tc_init_pack(tc, tcpack, config));
    run_h_tc(tc, outname, errname, "result");
    atf_tc_fini(tc);
}

ATF_TC(build_c_o);
ATF_TC_HEAD(build_c_o, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_check_build_c_o "
                      "function");
}
ATF_TC_BODY(build_c_o, tc)
{
    init_and_run_h_tc(&ATF_TC_NAME(h_build_c_o_ok),
             &ATF_TC_PACK_NAME(h_build_c_o_ok), "stdout", "stderr");
    ATF_CHECK(atf_utils_grep_file("-o test.o", "stdout"));
    ATF_CHECK(atf_utils_grep_file("-c test.c", "stdout"));

    init_and_run_h_tc(&ATF_TC_NAME(h_build_c_o_fail),
             &ATF_TC_PACK_NAME(h_build_c_o_fail), "stdout", "stderr");
    ATF_CHECK(atf_utils_grep_file("-o test.o", "stdout"));
    ATF_CHECK(atf_utils_grep_file("-c test.c", "stdout"));
    ATF_CHECK(atf_utils_grep_file("test.c", "stderr"));
    ATF_CHECK(atf_utils_grep_file("UNDEFINED_SYMBOL", "stderr"));
}

ATF_TC(build_cpp);
ATF_TC_HEAD(build_cpp, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_check_build_cpp "
                      "function");
}
ATF_TC_BODY(build_cpp, tc)
{
    init_and_run_h_tc(&ATF_TC_NAME(h_build_cpp_ok),
             &ATF_TC_PACK_NAME(h_build_cpp_ok), "stdout", "stderr");
    ATF_CHECK(atf_utils_grep_file("-o.*test.p", "stdout"));
    ATF_CHECK(atf_utils_grep_file("test.c", "stdout"));
    ATF_CHECK(atf_utils_grep_file("foo bar", "test.p"));

    init_and_run_h_tc(&ATF_TC_NAME(h_build_cpp_fail),
             &ATF_TC_PACK_NAME(h_build_cpp_fail), "stdout", "stderr");
    ATF_CHECK(atf_utils_grep_file("-o test.p", "stdout"));
    ATF_CHECK(atf_utils_grep_file("test.c", "stdout"));
    ATF_CHECK(atf_utils_grep_file("test.c", "stderr"));
    ATF_CHECK(atf_utils_grep_file("non-existent.h", "stderr"));
}

ATF_TC(build_cxx_o);
ATF_TC_HEAD(build_cxx_o, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_check_build_cxx_o "
                      "function");
}
ATF_TC_BODY(build_cxx_o, tc)
{
    init_and_run_h_tc(&ATF_TC_NAME(h_build_cxx_o_ok),
             &ATF_TC_PACK_NAME(h_build_cxx_o_ok), "stdout", "stderr");
    ATF_CHECK(atf_utils_grep_file("-o test.o", "stdout"));
    ATF_CHECK(atf_utils_grep_file("-c test.cpp", "stdout"));

    init_and_run_h_tc(&ATF_TC_NAME(h_build_cxx_o_fail),
             &ATF_TC_PACK_NAME(h_build_cxx_o_fail), "stdout", "stderr");
    ATF_CHECK(atf_utils_grep_file("-o test.o", "stdout"));
    ATF_CHECK(atf_utils_grep_file("-c test.cpp", "stdout"));
    ATF_CHECK(atf_utils_grep_file("test.cpp", "stderr"));
    ATF_CHECK(atf_utils_grep_file("UNDEFINED_SYMBOL", "stderr"));
}

ATF_TC(exec_array);
ATF_TC_HEAD(exec_array, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks that atf_check_exec_array "
                      "works properly");
}
ATF_TC_BODY(exec_array, tc)
{
    atf_fs_path_t process_helpers;
    atf_check_result_t result;

    get_process_helpers_path(tc, false, &process_helpers);

    const char *argv[4];
    argv[0] = atf_fs_path_cstring(&process_helpers);
    argv[1] = "echo";
    argv[2] = "test-message";
    argv[3] = NULL;

    RE(atf_check_exec_array(argv, &result));

    ATF_CHECK(atf_check_result_exited(&result));
    ATF_CHECK(atf_check_result_exitcode(&result) == EXIT_SUCCESS);

    {
        const char *path = atf_check_result_stdout(&result);
        int fd = open(path, O_RDONLY);
        ATF_CHECK(fd != -1);
        check_line(fd, "test-message");
        close(fd);
    }

    atf_check_result_fini(&result);
    atf_fs_path_fini(&process_helpers);
}

ATF_TC(exec_cleanup);
ATF_TC_HEAD(exec_cleanup, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks that atf_check_exec_array "
                      "properly cleans up the temporary files it creates");
}
ATF_TC_BODY(exec_cleanup, tc)
{
    atf_fs_path_t out, err;
    atf_check_result_t result;
    bool exists;

    do_exec(tc, "exit-success", &result);
    RE(atf_fs_path_init_fmt(&out, "%s", atf_check_result_stdout(&result)));
    RE(atf_fs_path_init_fmt(&err, "%s", atf_check_result_stderr(&result)));

    RE(atf_fs_exists(&out, &exists)); ATF_CHECK(exists);
    RE(atf_fs_exists(&err, &exists)); ATF_CHECK(exists);
    atf_check_result_fini(&result);
    RE(atf_fs_exists(&out, &exists)); ATF_CHECK(!exists);
    RE(atf_fs_exists(&err, &exists)); ATF_CHECK(!exists);

    atf_fs_path_fini(&err);
    atf_fs_path_fini(&out);
}

ATF_TC(exec_exitstatus);
ATF_TC_HEAD(exec_exitstatus, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks that atf_check_exec_array "
                      "properly captures the exit status of the executed "
                      "command");
}
ATF_TC_BODY(exec_exitstatus, tc)
{
    {
        atf_check_result_t result;
        do_exec(tc, "exit-success", &result);
        ATF_CHECK(atf_check_result_exited(&result));
        ATF_CHECK(!atf_check_result_signaled(&result));
        ATF_CHECK(atf_check_result_exitcode(&result) == EXIT_SUCCESS);
        atf_check_result_fini(&result);
    }

    {
        atf_check_result_t result;
        do_exec(tc, "exit-failure", &result);
        ATF_CHECK(atf_check_result_exited(&result));
        ATF_CHECK(!atf_check_result_signaled(&result));
        ATF_CHECK(atf_check_result_exitcode(&result) == EXIT_FAILURE);
        atf_check_result_fini(&result);
    }

    {
        atf_check_result_t result;
        do_exec(tc, "exit-signal", &result);
        ATF_CHECK(!atf_check_result_exited(&result));
        ATF_CHECK(atf_check_result_signaled(&result));
        ATF_CHECK(atf_check_result_termsig(&result) == SIGKILL);
        atf_check_result_fini(&result);
    }
}

ATF_TC(exec_stdout_stderr);
ATF_TC_HEAD(exec_stdout_stderr, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks that atf_check_exec_array "
                      "properly captures the stdout and stderr streams "
                      "of the child process");
}
ATF_TC_BODY(exec_stdout_stderr, tc)
{
    atf_check_result_t result1, result2;
    const char *out1, *out2;
    const char *err1, *err2;

    do_exec_with_arg(tc, "stdout-stderr", "result1", &result1);
    ATF_CHECK(atf_check_result_exited(&result1));
    ATF_CHECK(atf_check_result_exitcode(&result1) == EXIT_SUCCESS);

    do_exec_with_arg(tc, "stdout-stderr", "result2", &result2);
    ATF_CHECK(atf_check_result_exited(&result2));
    ATF_CHECK(atf_check_result_exitcode(&result2) == EXIT_SUCCESS);

    out1 = atf_check_result_stdout(&result1);
    out2 = atf_check_result_stdout(&result2);
    err1 = atf_check_result_stderr(&result1);
    err2 = atf_check_result_stderr(&result2);

    ATF_CHECK(strstr(out1, "check.XXXXXX") == NULL);
    ATF_CHECK(strstr(out2, "check.XXXXXX") == NULL);
    ATF_CHECK(strstr(err1, "check.XXXXXX") == NULL);
    ATF_CHECK(strstr(err2, "check.XXXXXX") == NULL);

    ATF_CHECK(strstr(out1, "/check") != NULL);
    ATF_CHECK(strstr(out2, "/check") != NULL);
    ATF_CHECK(strstr(err1, "/check") != NULL);
    ATF_CHECK(strstr(err2, "/check") != NULL);

    ATF_CHECK(strstr(out1, "/stdout") != NULL);
    ATF_CHECK(strstr(out2, "/stdout") != NULL);
    ATF_CHECK(strstr(err1, "/stderr") != NULL);
    ATF_CHECK(strstr(err2, "/stderr") != NULL);

    ATF_CHECK(strcmp(out1, out2) != 0);
    ATF_CHECK(strcmp(err1, err2) != 0);

#define CHECK_LINES(path, outname, resname) \
    do { \
        int fd = open(path, O_RDONLY); \
        ATF_CHECK(fd != -1); \
        check_line(fd, "Line 1 to " outname " for " resname); \
        check_line(fd, "Line 2 to " outname " for " resname); \
        close(fd); \
    } while (false)

    CHECK_LINES(out1, "stdout", "result1");
    CHECK_LINES(out2, "stdout", "result2");
    CHECK_LINES(err1, "stderr", "result1");
    CHECK_LINES(err2, "stderr", "result2");

#undef CHECK_LINES

    atf_check_result_fini(&result2);
    atf_check_result_fini(&result1);
}

ATF_TC(exec_umask);
ATF_TC_HEAD(exec_umask, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks that atf_check_exec_array "
                      "correctly reports an error if the umask is too "
                      "restrictive to create temporary files");
}
ATF_TC_BODY(exec_umask, tc)
{
    atf_check_result_t result;
    atf_fs_path_t process_helpers;
    const char *argv[3];

    get_process_helpers_path(tc, false, &process_helpers);
    argv[0] = atf_fs_path_cstring(&process_helpers);
    argv[1] = "exit-success";
    argv[2] = NULL;

    umask(0222);
    atf_error_t err = atf_check_exec_array(argv, &result);
    ATF_CHECK(atf_is_error(err));
    ATF_CHECK(atf_error_is(err, "invalid_umask"));
    atf_error_free(err);

    atf_fs_path_fini(&process_helpers);
}

ATF_TC(exec_unknown);
ATF_TC_HEAD(exec_unknown, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks that running a non-existing "
                      "binary is handled correctly");
}
ATF_TC_BODY(exec_unknown, tc)
{
    const char *argv[2];
    argv[0] = "/foo/bar/non-existent";
    argv[1] = NULL;

    atf_check_result_t result;
    RE(atf_check_exec_array(argv, &result));
    ATF_CHECK(atf_check_result_exited(&result));
    ATF_CHECK(atf_check_result_exitcode(&result) == 127);
    atf_check_result_fini(&result);
}

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{
    /* Add the test cases for the free functions. */
    ATF_TP_ADD_TC(tp, build_c_o);
    ATF_TP_ADD_TC(tp, build_cpp);
    ATF_TP_ADD_TC(tp, build_cxx_o);
    ATF_TP_ADD_TC(tp, exec_array);
    ATF_TP_ADD_TC(tp, exec_cleanup);
    ATF_TP_ADD_TC(tp, exec_exitstatus);
    ATF_TP_ADD_TC(tp, exec_stdout_stderr);
    ATF_TP_ADD_TC(tp, exec_umask);
    ATF_TP_ADD_TC(tp, exec_unknown);

    return atf_no_error();
}
