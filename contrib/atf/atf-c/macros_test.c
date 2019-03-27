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

#include "atf-c/macros.h"

#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "atf-c/detail/fs.h"
#include "atf-c/detail/process.h"
#include "atf-c/detail/test_helpers.h"
#include "atf-c/detail/text.h"

/* ---------------------------------------------------------------------
 * Auxiliary functions.
 * --------------------------------------------------------------------- */

static
void
create_ctl_file(const char *name)
{
    atf_fs_path_t p;

    RE(atf_fs_path_init_fmt(&p, "%s", name));
    ATF_REQUIRE(open(atf_fs_path_cstring(&p),
                   O_CREAT | O_WRONLY | O_TRUNC, 0644) != -1);
    atf_fs_path_fini(&p);
}

static
bool
exists(const char *p)
{
    bool b;
    atf_fs_path_t pp;

    RE(atf_fs_path_init_fmt(&pp, "%s", p));
    RE(atf_fs_exists(&pp, &b));
    atf_fs_path_fini(&pp);

    return b;
}

static
void
init_and_run_h_tc(const char *name, void (*head)(atf_tc_t *),
                  void (*body)(const atf_tc_t *))
{
    atf_tc_t tc;
    const char *const config[] = { NULL };

    RE(atf_tc_init(&tc, name, head, body, NULL, config));
    run_h_tc(&tc, "output", "error", "result");
    atf_tc_fini(&tc);
}

/* ---------------------------------------------------------------------
 * Helper test cases.
 * --------------------------------------------------------------------- */

#define H_DEF(id, macro) \
    ATF_TC_HEAD(h_ ## id, tc) \
    { \
        atf_tc_set_md_var(tc, "descr", "Helper test case"); \
    } \
    ATF_TC_BODY(h_ ## id, tc) \
    { \
        create_ctl_file("before"); \
        macro; \
        create_ctl_file("after"); \
    }

#define H_CHECK_HEAD_NAME(id) ATF_TC_HEAD_NAME(h_check_ ## id)
#define H_CHECK_BODY_NAME(id) ATF_TC_BODY_NAME(h_check_ ## id)
#define H_CHECK(id, condition) \
    H_DEF(check_ ## id, ATF_CHECK(condition))

#define H_CHECK_MSG_HEAD_NAME(id) ATF_TC_HEAD_NAME(h_check_msg_ ## id)
#define H_CHECK_MSG_BODY_NAME(id) ATF_TC_BODY_NAME(h_check_msg_ ## id)
#define H_CHECK_MSG(id, condition, msg) \
    H_DEF(check_msg_ ## id, ATF_CHECK_MSG(condition, msg))

#define H_CHECK_EQ_HEAD_NAME(id) ATF_TC_HEAD_NAME(h_check_eq_ ## id)
#define H_CHECK_EQ_BODY_NAME(id) ATF_TC_BODY_NAME(h_check_eq_ ## id)
#define H_CHECK_EQ(id, v1, v2) \
    H_DEF(check_eq_ ## id, ATF_CHECK_EQ(v1, v2))

#define H_CHECK_STREQ_HEAD_NAME(id) ATF_TC_HEAD_NAME(h_check_streq_ ## id)
#define H_CHECK_STREQ_BODY_NAME(id) ATF_TC_BODY_NAME(h_check_streq_ ## id)
#define H_CHECK_STREQ(id, v1, v2) \
    H_DEF(check_streq_ ## id, ATF_CHECK_STREQ(v1, v2))

#define H_CHECK_MATCH_HEAD_NAME(id) ATF_TC_HEAD_NAME(h_check_match_ ## id)
#define H_CHECK_MATCH_BODY_NAME(id) ATF_TC_BODY_NAME(h_check_match_ ## id)
#define H_CHECK_MATCH(id, v1, v2) \
    H_DEF(check_match_ ## id, ATF_CHECK_MATCH(v1, v2))

#define H_CHECK_EQ_MSG_HEAD_NAME(id) \
    ATF_TC_HEAD_NAME(h_check_eq_msg_ ## id)
#define H_CHECK_EQ_MSG_BODY_NAME(id) \
    ATF_TC_BODY_NAME(h_check_eq_msg_ ## id)
#define H_CHECK_EQ_MSG(id, v1, v2, msg) \
    H_DEF(check_eq_msg_ ## id, ATF_CHECK_EQ_MSG(v1, v2, msg))

#define H_CHECK_STREQ_MSG_HEAD_NAME(id) \
    ATF_TC_HEAD_NAME(h_check_streq_msg_ ## id)
#define H_CHECK_STREQ_MSG_BODY_NAME(id) \
    ATF_TC_BODY_NAME(h_check_streq_msg_ ## id)
#define H_CHECK_STREQ_MSG(id, v1, v2, msg) \
    H_DEF(check_streq_msg_ ## id, ATF_CHECK_STREQ_MSG(v1, v2, msg))

#define H_CHECK_MATCH_MSG_HEAD_NAME(id) \
    ATF_TC_HEAD_NAME(h_check_match_msg_ ## id)
#define H_CHECK_MATCH_MSG_BODY_NAME(id) \
    ATF_TC_BODY_NAME(h_check_match_msg_ ## id)
#define H_CHECK_MATCH_MSG(id, v1, v2, msg) \
    H_DEF(check_match_msg_ ## id, ATF_CHECK_MATCH_MSG(v1, v2, msg))

#define H_CHECK_ERRNO_HEAD_NAME(id) ATF_TC_HEAD_NAME(h_check_errno_ ## id)
#define H_CHECK_ERRNO_BODY_NAME(id) ATF_TC_BODY_NAME(h_check_errno_ ## id)
#define H_CHECK_ERRNO(id, exp_errno, bool_expr) \
    H_DEF(check_errno_ ## id, ATF_CHECK_ERRNO(exp_errno, bool_expr))

#define H_REQUIRE_HEAD_NAME(id) ATF_TC_HEAD_NAME(h_require_ ## id)
#define H_REQUIRE_BODY_NAME(id) ATF_TC_BODY_NAME(h_require_ ## id)
#define H_REQUIRE(id, condition) \
    H_DEF(require_ ## id, ATF_REQUIRE(condition))

#define H_REQUIRE_MSG_HEAD_NAME(id) ATF_TC_HEAD_NAME(h_require_msg_ ## id)
#define H_REQUIRE_MSG_BODY_NAME(id) ATF_TC_BODY_NAME(h_require_msg_ ## id)
#define H_REQUIRE_MSG(id, condition, msg) \
    H_DEF(require_msg_ ## id, ATF_REQUIRE_MSG(condition, msg))

#define H_REQUIRE_EQ_HEAD_NAME(id) ATF_TC_HEAD_NAME(h_require_eq_ ## id)
#define H_REQUIRE_EQ_BODY_NAME(id) ATF_TC_BODY_NAME(h_require_eq_ ## id)
#define H_REQUIRE_EQ(id, v1, v2) \
    H_DEF(require_eq_ ## id, ATF_REQUIRE_EQ(v1, v2))

#define H_REQUIRE_STREQ_HEAD_NAME(id) ATF_TC_HEAD_NAME(h_require_streq_ ## id)
#define H_REQUIRE_STREQ_BODY_NAME(id) ATF_TC_BODY_NAME(h_require_streq_ ## id)
#define H_REQUIRE_STREQ(id, v1, v2) \
    H_DEF(require_streq_ ## id, ATF_REQUIRE_STREQ(v1, v2))

#define H_REQUIRE_MATCH_HEAD_NAME(id) ATF_TC_HEAD_NAME(h_require_match_ ## id)
#define H_REQUIRE_MATCH_BODY_NAME(id) ATF_TC_BODY_NAME(h_require_match_ ## id)
#define H_REQUIRE_MATCH(id, v1, v2) \
    H_DEF(require_match_ ## id, ATF_REQUIRE_MATCH(v1, v2))

#define H_REQUIRE_EQ_MSG_HEAD_NAME(id) \
    ATF_TC_HEAD_NAME(h_require_eq_msg_ ## id)
#define H_REQUIRE_EQ_MSG_BODY_NAME(id) \
    ATF_TC_BODY_NAME(h_require_eq_msg_ ## id)
#define H_REQUIRE_EQ_MSG(id, v1, v2, msg) \
    H_DEF(require_eq_msg_ ## id, ATF_REQUIRE_EQ_MSG(v1, v2, msg))

#define H_REQUIRE_STREQ_MSG_HEAD_NAME(id) \
    ATF_TC_HEAD_NAME(h_require_streq_msg_ ## id)
#define H_REQUIRE_STREQ_MSG_BODY_NAME(id) \
    ATF_TC_BODY_NAME(h_require_streq_msg_ ## id)
#define H_REQUIRE_STREQ_MSG(id, v1, v2, msg) \
    H_DEF(require_streq_msg_ ## id, ATF_REQUIRE_STREQ_MSG(v1, v2, msg))

#define H_REQUIRE_MATCH_MSG_HEAD_NAME(id) \
    ATF_TC_HEAD_NAME(h_require_match_msg_ ## id)
#define H_REQUIRE_MATCH_MSG_BODY_NAME(id) \
    ATF_TC_BODY_NAME(h_require_match_msg_ ## id)
#define H_REQUIRE_MATCH_MSG(id, v1, v2, msg) \
    H_DEF(require_match_msg_ ## id, ATF_REQUIRE_MATCH_MSG(v1, v2, msg))

#define H_REQUIRE_ERRNO_HEAD_NAME(id) ATF_TC_HEAD_NAME(h_require_errno_ ## id)
#define H_REQUIRE_ERRNO_BODY_NAME(id) ATF_TC_BODY_NAME(h_require_errno_ ## id)
#define H_REQUIRE_ERRNO(id, exp_errno, bool_expr) \
    H_DEF(require_errno_ ## id, ATF_REQUIRE_ERRNO(exp_errno, bool_expr))

/* ---------------------------------------------------------------------
 * Test cases for the ATF_{CHECK,REQUIRE}_ERRNO macros.
 * --------------------------------------------------------------------- */

static int
errno_fail_stub(const int raised_errno)
{
    errno = raised_errno;
    return -1;
}

static int
errno_ok_stub(void)
{
    return 0;
}

H_CHECK_ERRNO(no_error, -1, errno_ok_stub() == -1);
H_CHECK_ERRNO(errno_ok, 2, errno_fail_stub(2) == -1);
H_CHECK_ERRNO(errno_fail, 3, errno_fail_stub(4) == -1);

H_REQUIRE_ERRNO(no_error, -1, errno_ok_stub() == -1);
H_REQUIRE_ERRNO(errno_ok, 2, errno_fail_stub(2) == -1);
H_REQUIRE_ERRNO(errno_fail, 3, errno_fail_stub(4) == -1);

ATF_TC(check_errno);
ATF_TC_HEAD(check_errno, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the ATF_CHECK_ERRNO macro");
}
ATF_TC_BODY(check_errno, tc)
{
    struct test {
        void (*head)(atf_tc_t *);
        void (*body)(const atf_tc_t *);
        bool ok;
        const char *exp_regex;
    } *t, tests[] = {
        { H_CHECK_ERRNO_HEAD_NAME(no_error),
          H_CHECK_ERRNO_BODY_NAME(no_error),
          false, "Expected true value in errno_ok_stub\\(\\) == -1" },
        { H_CHECK_ERRNO_HEAD_NAME(errno_ok),
          H_CHECK_ERRNO_BODY_NAME(errno_ok),
          true, NULL },
        { H_CHECK_ERRNO_HEAD_NAME(errno_fail),
          H_CHECK_ERRNO_BODY_NAME(errno_fail),
          false, "Expected errno 3, got 4, in errno_fail_stub\\(4\\) == -1" },
        { NULL, NULL, false, NULL }
    };

    for (t = &tests[0]; t->head != NULL; t++) {
        init_and_run_h_tc("h_check_errno", t->head, t->body);

        ATF_REQUIRE(exists("before"));
        ATF_REQUIRE(exists("after"));

        if (t->ok) {
            ATF_REQUIRE(atf_utils_grep_file("^passed", "result"));
        } else {
            ATF_REQUIRE(atf_utils_grep_file("^failed", "result"));
            ATF_REQUIRE(atf_utils_grep_file(
                "macros_test.c:[0-9]+: %s$", "error", t->exp_regex));
        }

        ATF_REQUIRE(unlink("before") != -1);
        ATF_REQUIRE(unlink("after") != -1);
    }
}

ATF_TC(require_errno);
ATF_TC_HEAD(require_errno, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the ATF_REQUIRE_ERRNO macro");
}
ATF_TC_BODY(require_errno, tc)
{
    struct test {
        void (*head)(atf_tc_t *);
        void (*body)(const atf_tc_t *);
        bool ok;
        const char *exp_regex;
    } *t, tests[] = {
        { H_REQUIRE_ERRNO_HEAD_NAME(no_error),
          H_REQUIRE_ERRNO_BODY_NAME(no_error),
          false, "Expected true value in errno_ok_stub\\(\\) == -1" },
        { H_REQUIRE_ERRNO_HEAD_NAME(errno_ok),
          H_REQUIRE_ERRNO_BODY_NAME(errno_ok),
          true, NULL },
        { H_REQUIRE_ERRNO_HEAD_NAME(errno_fail),
          H_REQUIRE_ERRNO_BODY_NAME(errno_fail),
          false, "Expected errno 3, got 4, in errno_fail_stub\\(4\\) == -1" },
        { NULL, NULL, false, NULL }
    };

    for (t = &tests[0]; t->head != NULL; t++) {
        init_and_run_h_tc("h_require_errno", t->head, t->body);

        ATF_REQUIRE(exists("before"));
        if (t->ok) {
            ATF_REQUIRE(atf_utils_grep_file("^passed", "result"));
            ATF_REQUIRE(exists("after"));
        } else {
            ATF_REQUIRE(atf_utils_grep_file(
                "^failed: .*macros_test.c:[0-9]+: %s$", "result",
                t->exp_regex));
            ATF_REQUIRE(!exists("after"));
        }

        ATF_REQUIRE(unlink("before") != -1);
        if (t->ok)
            ATF_REQUIRE(unlink("after") != -1);
    }
}

/* ---------------------------------------------------------------------
 * Test cases for the ATF_CHECK and ATF_CHECK_MSG macros.
 * --------------------------------------------------------------------- */

H_CHECK(0, 0);
H_CHECK(1, 1);
H_CHECK_MSG(0, 0, "expected a false value");
H_CHECK_MSG(1, 1, "expected a true value");

ATF_TC(check);
ATF_TC_HEAD(check, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the ATF_CHECK and "
                      "ATF_CHECK_MSG macros");
}
ATF_TC_BODY(check, tc)
{
    struct test {
        void (*head)(atf_tc_t *);
        void (*body)(const atf_tc_t *);
        bool value;
        const char *msg;
        bool ok;
    } *t, tests[] = {
        { H_CHECK_HEAD_NAME(0), H_CHECK_BODY_NAME(0), 0,
          "0 not met", false },
        { H_CHECK_HEAD_NAME(1), H_CHECK_BODY_NAME(1), 1,
          "1 not met", true },
        { H_CHECK_MSG_HEAD_NAME(0), H_CHECK_MSG_BODY_NAME(0), 0,
          "expected a false value", false },
        { H_CHECK_MSG_HEAD_NAME(1), H_CHECK_MSG_BODY_NAME(1), 1,
          "expected a true value", true },
        { NULL, NULL, false, NULL, false }
    };

    for (t = &tests[0]; t->head != NULL; t++) {
        printf("Checking with a %d value\n", t->value);

        init_and_run_h_tc("h_check", t->head, t->body);

        ATF_REQUIRE(exists("before"));
        ATF_REQUIRE(exists("after"));

        if (t->ok) {
            ATF_REQUIRE(atf_utils_grep_file("^passed", "result"));
        } else {
            ATF_REQUIRE(atf_utils_grep_file("^failed", "result"));
            ATF_REQUIRE(atf_utils_grep_file("Check failed: .*"
                "macros_test.c:[0-9]+: %s$", "error", t->msg));
        }

        ATF_REQUIRE(unlink("before") != -1);
        ATF_REQUIRE(unlink("after") != -1);
    }
}

/* ---------------------------------------------------------------------
 * Test cases for the ATF_CHECK_*EQ_ macros.
 * --------------------------------------------------------------------- */

struct check_eq_test {
    void (*head)(atf_tc_t *);
    void (*body)(const atf_tc_t *);
    const char *v1;
    const char *v2;
    const char *msg;
    bool ok;
};

static
void
do_check_eq_tests(const struct check_eq_test *tests)
{
    const struct check_eq_test *t;

    for (t = &tests[0]; t->head != NULL; t++) {
        printf("Checking with %s, %s and expecting %s\n", t->v1, t->v2,
               t->ok ? "true" : "false");

        init_and_run_h_tc("h_check", t->head, t->body);

        ATF_CHECK(exists("before"));
        ATF_CHECK(exists("after"));

        if (t->ok) {
            ATF_REQUIRE(atf_utils_grep_file("^passed", "result"));
        } else {
            ATF_REQUIRE(atf_utils_grep_file("^failed", "result"));
            ATF_CHECK(atf_utils_grep_file("Check failed: .*"
                "macros_test.c:[0-9]+: %s$", "error", t->msg));
        }

        ATF_CHECK(unlink("before") != -1);
        ATF_CHECK(unlink("after") != -1);
    }
}

H_CHECK_EQ(1_1, 1, 1);
H_CHECK_EQ(1_2, 1, 2);
H_CHECK_EQ(2_1, 2, 1);
H_CHECK_EQ(2_2, 2, 2);
H_CHECK_EQ_MSG(1_1, 1, 1, "1 does not match 1");
H_CHECK_EQ_MSG(1_2, 1, 2, "1 does not match 2");
H_CHECK_EQ_MSG(2_1, 2, 1, "2 does not match 1");
H_CHECK_EQ_MSG(2_2, 2, 2, "2 does not match 2");

ATF_TC(check_eq);
ATF_TC_HEAD(check_eq, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the ATF_CHECK_EQ and "
                      "ATF_CHECK_EQ_MSG macros");
}
ATF_TC_BODY(check_eq, tc)
{
    struct check_eq_test tests[] = {
        { H_CHECK_EQ_HEAD_NAME(1_1), H_CHECK_EQ_BODY_NAME(1_1),
          "1", "1", "1 != 1", true },
        { H_CHECK_EQ_HEAD_NAME(1_2), H_CHECK_EQ_BODY_NAME(1_2),
          "1", "2", "1 != 2", false },
        { H_CHECK_EQ_HEAD_NAME(2_1), H_CHECK_EQ_BODY_NAME(2_1),
          "2", "1", "2 != 1", false },
        { H_CHECK_EQ_HEAD_NAME(2_2), H_CHECK_EQ_BODY_NAME(2_2),
          "2", "2", "2 != 2", true },
        { H_CHECK_EQ_MSG_HEAD_NAME(1_1), H_CHECK_EQ_MSG_BODY_NAME(1_1),
          "1", "1", "1 != 1: 1 does not match 1", true },
        { H_CHECK_EQ_MSG_HEAD_NAME(1_2), H_CHECK_EQ_MSG_BODY_NAME(1_2),
          "1", "2", "1 != 2: 1 does not match 2", false },
        { H_CHECK_EQ_MSG_HEAD_NAME(2_1), H_CHECK_EQ_MSG_BODY_NAME(2_1),
          "2", "1", "2 != 1: 2 does not match 1", false },
        { H_CHECK_EQ_MSG_HEAD_NAME(2_2), H_CHECK_EQ_MSG_BODY_NAME(2_2),
          "2", "2", "2 != 2: 2 does not match 2", true },
        { NULL, NULL, 0, 0, "", false }
    };
    do_check_eq_tests(tests);
}

H_CHECK_STREQ(1_1, "1", "1");
H_CHECK_STREQ(1_2, "1", "2");
H_CHECK_STREQ(2_1, "2", "1");
H_CHECK_STREQ(2_2, "2", "2");
H_CHECK_STREQ_MSG(1_1, "1", "1", "1 does not match 1");
H_CHECK_STREQ_MSG(1_2, "1", "2", "1 does not match 2");
H_CHECK_STREQ_MSG(2_1, "2", "1", "2 does not match 1");
H_CHECK_STREQ_MSG(2_2, "2", "2", "2 does not match 2");
#define CHECK_STREQ_VAR1 "5"
#define CHECK_STREQ_VAR2 "9"
const char *check_streq_var1 = CHECK_STREQ_VAR1;
const char *check_streq_var2 = CHECK_STREQ_VAR2;
H_CHECK_STREQ(vars, check_streq_var1, check_streq_var2);

ATF_TC(check_streq);
ATF_TC_HEAD(check_streq, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the ATF_CHECK_STREQ and "
                      "ATF_CHECK_STREQ_MSG macros");
}
ATF_TC_BODY(check_streq, tc)
{
    struct check_eq_test tests[] = {
        { H_CHECK_STREQ_HEAD_NAME(1_1), H_CHECK_STREQ_BODY_NAME(1_1),
          "1", "1", "\"1\" != \"1\" \\(1 != 1\\)", true },
        { H_CHECK_STREQ_HEAD_NAME(1_2), H_CHECK_STREQ_BODY_NAME(1_2),
          "1", "2", "\"1\" != \"2\" \\(1 != 2\\)", false },
        { H_CHECK_STREQ_HEAD_NAME(2_1), H_CHECK_STREQ_BODY_NAME(2_1),
          "2", "1", "\"2\" != \"1\" \\(2 != 1\\)", false },
        { H_CHECK_STREQ_HEAD_NAME(2_2), H_CHECK_STREQ_BODY_NAME(2_2),
          "2", "2", "\"2\" != \"2\" \\(2 != 2\\)", true },
        { H_CHECK_STREQ_MSG_HEAD_NAME(1_1),
          H_CHECK_STREQ_MSG_BODY_NAME(1_1),
          "1", "1", "\"1\" != \"1\" \\(1 != 1\\): 1 does not match 1", true },
        { H_CHECK_STREQ_MSG_HEAD_NAME(1_2),
          H_CHECK_STREQ_MSG_BODY_NAME(1_2),
          "1", "2", "\"1\" != \"2\" \\(1 != 2\\): 1 does not match 2", false },
        { H_CHECK_STREQ_MSG_HEAD_NAME(2_1),
          H_CHECK_STREQ_MSG_BODY_NAME(2_1),
          "2", "1", "\"2\" != \"1\" \\(2 != 1\\): 2 does not match 1", false },
        { H_CHECK_STREQ_MSG_HEAD_NAME(2_2),
          H_CHECK_STREQ_MSG_BODY_NAME(2_2),
          "2", "2", "\"2\" != \"2\" \\(2 != 2\\): 2 does not match 2", true },
        { H_CHECK_STREQ_HEAD_NAME(vars), H_CHECK_STREQ_BODY_NAME(vars),
          check_streq_var1, check_streq_var2,
          "check_streq_var1 != check_streq_var2 \\("
          CHECK_STREQ_VAR1 " != " CHECK_STREQ_VAR2 "\\)", false },
        { NULL, NULL, 0, 0, "", false }
    };
    do_check_eq_tests(tests);
}

/* ---------------------------------------------------------------------
 * Test cases for the ATF_CHECK_MATCH and ATF_CHECK_MATCH_MSG macros.
 * --------------------------------------------------------------------- */

H_CHECK_MATCH(yes, "hello [a-z]+", "abc hello world");
H_CHECK_MATCH(no, "hello [a-z]+", "abc hello WORLD");
H_CHECK_MATCH_MSG(yes, "hello [a-z]+", "abc hello world", "lowercase");
H_CHECK_MATCH_MSG(no, "hello [a-z]+", "abc hello WORLD", "uppercase");

ATF_TC(check_match);
ATF_TC_HEAD(check_match, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the ATF_CHECK_MATCH and "
                      "ATF_CHECK_MATCH_MSG macros");
}
ATF_TC_BODY(check_match, tc)
{
    struct check_eq_test tests[] = {
        { H_CHECK_MATCH_HEAD_NAME(yes), H_CHECK_MATCH_BODY_NAME(yes),
          "hello [a-z]+", "abc hello world", "", true },
        { H_CHECK_MATCH_HEAD_NAME(no), H_CHECK_MATCH_BODY_NAME(no),
          "hello [a-z]+", "abc hello WORLD",
          "'hello \\[a-z\\]\\+' not matched in 'abc hello WORLD'", false },
        { H_CHECK_MATCH_MSG_HEAD_NAME(yes), H_CHECK_MATCH_MSG_BODY_NAME(yes),
          "hello [a-z]+", "abc hello world", "", true },
        { H_CHECK_MATCH_MSG_HEAD_NAME(no), H_CHECK_MATCH_MSG_BODY_NAME(no),
          "hello [a-z]+", "abc hello WORLD",
          "'hello \\[a-z\\]\\+' not matched in 'abc hello WORLD': uppercase",
          false },
        { NULL, NULL, 0, 0, "", false }
    };
    do_check_eq_tests(tests);
}

/* ---------------------------------------------------------------------
 * Test cases for the ATF_REQUIRE and ATF_REQUIRE_MSG macros.
 * --------------------------------------------------------------------- */

H_REQUIRE(0, 0);
H_REQUIRE(1, 1);
H_REQUIRE_MSG(0, 0, "expected a false value");
H_REQUIRE_MSG(1, 1, "expected a true value");

ATF_TC(require);
ATF_TC_HEAD(require, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the ATF_REQUIRE and "
                      "ATF_REQUIRE_MSG macros");
}
ATF_TC_BODY(require, tc)
{
    struct test {
        void (*head)(atf_tc_t *);
        void (*body)(const atf_tc_t *);
        bool value;
        const char *msg;
        bool ok;
    } *t, tests[] = {
        { H_REQUIRE_HEAD_NAME(0), H_REQUIRE_BODY_NAME(0), 0,
          "0 not met", false },
        { H_REQUIRE_HEAD_NAME(1), H_REQUIRE_BODY_NAME(1), 1,
          "1 not met", true },
        { H_REQUIRE_MSG_HEAD_NAME(0), H_REQUIRE_MSG_BODY_NAME(0), 0,
          "expected a false value", false },
        { H_REQUIRE_MSG_HEAD_NAME(1), H_REQUIRE_MSG_BODY_NAME(1), 1,
          "expected a true value", true },
        { NULL, NULL, false, NULL, false }
    };

    for (t = &tests[0]; t->head != NULL; t++) {
        printf("Checking with a %d value\n", t->value);

        init_and_run_h_tc("h_require", t->head, t->body);

        ATF_REQUIRE(exists("before"));
        if (t->ok) {
            ATF_REQUIRE(atf_utils_grep_file("^passed", "result"));
            ATF_REQUIRE(exists("after"));
        } else {
            ATF_REQUIRE(atf_utils_grep_file(
                "^failed: .*macros_test.c:[0-9]+: %s$", "result", t->msg));
            ATF_REQUIRE(!exists("after"));
        }

        ATF_REQUIRE(unlink("before") != -1);
        if (t->ok)
            ATF_REQUIRE(unlink("after") != -1);
    }
}

/* ---------------------------------------------------------------------
 * Test cases for the ATF_REQUIRE_*EQ_ macros.
 * --------------------------------------------------------------------- */

struct require_eq_test {
    void (*head)(atf_tc_t *);
    void (*body)(const atf_tc_t *);
    const char *v1;
    const char *v2;
    const char *msg;
    bool ok;
};

static
void
do_require_eq_tests(const struct require_eq_test *tests)
{
    const struct require_eq_test *t;

    for (t = &tests[0]; t->head != NULL; t++) {
        printf("Checking with %s, %s and expecting %s\n", t->v1, t->v2,
               t->ok ? "true" : "false");

        init_and_run_h_tc("h_require", t->head, t->body);

        ATF_REQUIRE(exists("before"));
        if (t->ok) {
            ATF_REQUIRE(atf_utils_grep_file("^passed", "result"));
            ATF_REQUIRE(exists("after"));
        } else {
            ATF_REQUIRE(atf_utils_grep_file("^failed: .*macros_test.c"
                ":[0-9]+: %s$", "result", t->msg));
            ATF_REQUIRE(!exists("after"));
        }

        ATF_REQUIRE(unlink("before") != -1);
        if (t->ok)
            ATF_REQUIRE(unlink("after") != -1);
    }
}

H_REQUIRE_EQ(1_1, 1, 1);
H_REQUIRE_EQ(1_2, 1, 2);
H_REQUIRE_EQ(2_1, 2, 1);
H_REQUIRE_EQ(2_2, 2, 2);
H_REQUIRE_EQ_MSG(1_1, 1, 1, "1 does not match 1");
H_REQUIRE_EQ_MSG(1_2, 1, 2, "1 does not match 2");
H_REQUIRE_EQ_MSG(2_1, 2, 1, "2 does not match 1");
H_REQUIRE_EQ_MSG(2_2, 2, 2, "2 does not match 2");

ATF_TC(require_eq);
ATF_TC_HEAD(require_eq, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the ATF_REQUIRE_EQ and "
                      "ATF_REQUIRE_EQ_MSG macros");
}
ATF_TC_BODY(require_eq, tc)
{
    struct require_eq_test tests[] = {
        { H_REQUIRE_EQ_HEAD_NAME(1_1), H_REQUIRE_EQ_BODY_NAME(1_1),
          "1", "1", "1 != 1", true },
        { H_REQUIRE_EQ_HEAD_NAME(1_2), H_REQUIRE_EQ_BODY_NAME(1_2),
          "1", "2", "1 != 2", false },
        { H_REQUIRE_EQ_HEAD_NAME(2_1), H_REQUIRE_EQ_BODY_NAME(2_1),
          "2", "1", "2 != 1", false },
        { H_REQUIRE_EQ_HEAD_NAME(2_2), H_REQUIRE_EQ_BODY_NAME(2_2),
          "2", "2", "2 != 2", true },
        { H_REQUIRE_EQ_MSG_HEAD_NAME(1_1), H_REQUIRE_EQ_MSG_BODY_NAME(1_1),
          "1", "1", "1 != 1: 1 does not match 1", true },
        { H_REQUIRE_EQ_MSG_HEAD_NAME(1_2), H_REQUIRE_EQ_MSG_BODY_NAME(1_2),
          "1", "2", "1 != 2: 1 does not match 2", false },
        { H_REQUIRE_EQ_MSG_HEAD_NAME(2_1), H_REQUIRE_EQ_MSG_BODY_NAME(2_1),
          "2", "1", "2 != 1: 2 does not match 1", false },
        { H_REQUIRE_EQ_MSG_HEAD_NAME(2_2), H_REQUIRE_EQ_MSG_BODY_NAME(2_2),
          "2", "2", "2 != 2: 2 does not match 2", true },
        { NULL, NULL, 0, 0, "", false }
    };
    do_require_eq_tests(tests);
}

H_REQUIRE_STREQ(1_1, "1", "1");
H_REQUIRE_STREQ(1_2, "1", "2");
H_REQUIRE_STREQ(2_1, "2", "1");
H_REQUIRE_STREQ(2_2, "2", "2");
H_REQUIRE_STREQ_MSG(1_1, "1", "1", "1 does not match 1");
H_REQUIRE_STREQ_MSG(1_2, "1", "2", "1 does not match 2");
H_REQUIRE_STREQ_MSG(2_1, "2", "1", "2 does not match 1");
H_REQUIRE_STREQ_MSG(2_2, "2", "2", "2 does not match 2");
#define REQUIRE_STREQ_VAR1 "5"
#define REQUIRE_STREQ_VAR2 "9"
const char *require_streq_var1 = REQUIRE_STREQ_VAR1;
const char *require_streq_var2 = REQUIRE_STREQ_VAR2;
H_REQUIRE_STREQ(vars, require_streq_var1, require_streq_var2);

ATF_TC(require_streq);
ATF_TC_HEAD(require_streq, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the ATF_REQUIRE_STREQ and "
                      "ATF_REQUIRE_STREQ_MSG macros");
}
ATF_TC_BODY(require_streq, tc)
{
    struct require_eq_test tests[] = {
        { H_REQUIRE_STREQ_HEAD_NAME(1_1), H_REQUIRE_STREQ_BODY_NAME(1_1),
          "1", "1", "\"1\" != \"1\" \\(1 != 1\\)", true },
        { H_REQUIRE_STREQ_HEAD_NAME(1_2), H_REQUIRE_STREQ_BODY_NAME(1_2),
          "1", "2", "\"1\" != \"2\" \\(1 != 2\\)", false },
        { H_REQUIRE_STREQ_HEAD_NAME(2_1), H_REQUIRE_STREQ_BODY_NAME(2_1),
          "2", "1", "\"2\" != \"1\" \\(2 != 1\\)", false },
        { H_REQUIRE_STREQ_HEAD_NAME(2_2), H_REQUIRE_STREQ_BODY_NAME(2_2),
          "2", "2", "\"2\" != \"2\" \\(2 != 2\\)", true },
        { H_REQUIRE_STREQ_MSG_HEAD_NAME(1_1),
          H_REQUIRE_STREQ_MSG_BODY_NAME(1_1),
          "1", "1", "\"1\" != \"1\" \\(1 != 1\\): 1 does not match 1", true },
        { H_REQUIRE_STREQ_MSG_HEAD_NAME(1_2),
          H_REQUIRE_STREQ_MSG_BODY_NAME(1_2),
          "1", "2", "\"1\" != \"2\" \\(1 != 2\\): 1 does not match 2", false },
        { H_REQUIRE_STREQ_MSG_HEAD_NAME(2_1),
          H_REQUIRE_STREQ_MSG_BODY_NAME(2_1),
          "2", "1", "\"2\" != \"1\" \\(2 != 1\\): 2 does not match 1", false },
        { H_REQUIRE_STREQ_MSG_HEAD_NAME(2_2),
          H_REQUIRE_STREQ_MSG_BODY_NAME(2_2),
          "2", "2", "\"2\" != \"2\" \\(2 != 2\\): 2 does not match 2", true },
        { H_REQUIRE_STREQ_HEAD_NAME(vars), H_REQUIRE_STREQ_BODY_NAME(vars),
          require_streq_var1, require_streq_var2,
          "require_streq_var1 != require_streq_var2 \\("
          REQUIRE_STREQ_VAR1 " != " REQUIRE_STREQ_VAR2 "\\)", false },
        { NULL, NULL, 0, 0, "", false }
    };
    do_require_eq_tests(tests);
}

/* ---------------------------------------------------------------------
 * Test cases for the ATF_REQUIRE_MATCH and ATF_REQUIRE_MATCH_MSG macros.
 * --------------------------------------------------------------------- */

H_REQUIRE_MATCH(yes, "hello [a-z]+", "abc hello world");
H_REQUIRE_MATCH(no, "hello [a-z]+", "abc hello WORLD");
H_REQUIRE_MATCH_MSG(yes, "hello [a-z]+", "abc hello world", "lowercase");
H_REQUIRE_MATCH_MSG(no, "hello [a-z]+", "abc hello WORLD", "uppercase");

ATF_TC(require_match);
ATF_TC_HEAD(require_match, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the ATF_REQUIRE_MATCH and "
                      "ATF_REQUIRE_MATCH_MSG macros");
}
ATF_TC_BODY(require_match, tc)
{
    struct require_eq_test tests[] = {
        { H_REQUIRE_MATCH_HEAD_NAME(yes), H_REQUIRE_MATCH_BODY_NAME(yes),
          "hello [a-z]+", "abc hello world", "", true },
        { H_REQUIRE_MATCH_HEAD_NAME(no), H_REQUIRE_MATCH_BODY_NAME(no),
          "hello [a-z]+", "abc hello WORLD",
          "'hello \\[a-z\\]\\+' not matched in 'abc hello WORLD'", false },
        { H_REQUIRE_MATCH_MSG_HEAD_NAME(yes),
          H_REQUIRE_MATCH_MSG_BODY_NAME(yes),
          "hello [a-z]+", "abc hello world", "", true },
        { H_REQUIRE_MATCH_MSG_HEAD_NAME(no), H_REQUIRE_MATCH_MSG_BODY_NAME(no),
          "hello [a-z]+", "abc hello WORLD",
          "'hello \\[a-z\\]\\+' not matched in 'abc hello WORLD': uppercase",
          false },
        { NULL, NULL, 0, 0, "", false }
    };
    do_require_eq_tests(tests);
}

/* ---------------------------------------------------------------------
 * Miscellaneous test cases covering several macros.
 * --------------------------------------------------------------------- */

static
bool
aux_bool(const char *fmt ATF_DEFS_ATTRIBUTE_UNUSED)
{
    return false;
}

static
const char *
aux_str(const char *fmt ATF_DEFS_ATTRIBUTE_UNUSED)
{
    return "foo";
}

H_CHECK(msg, aux_bool("%d"));
H_REQUIRE(msg, aux_bool("%d"));
H_CHECK_STREQ(msg, aux_str("%d"), "");
H_REQUIRE_STREQ(msg, aux_str("%d"), "");

ATF_TC(msg_embedded_fmt);
ATF_TC_HEAD(msg_embedded_fmt, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests that format strings passed "
                      "as part of the automatically-generated messages "
                      "do not get expanded");
}
ATF_TC_BODY(msg_embedded_fmt, tc)
{
    struct test {
        void (*head)(atf_tc_t *);
        void (*body)(const atf_tc_t *);
        bool fatal;
        const char *msg;
    } *t, tests[] = {
       {  H_CHECK_HEAD_NAME(msg), H_CHECK_BODY_NAME(msg), false,
          "aux_bool\\(\"%d\"\\) not met" },
       {  H_REQUIRE_HEAD_NAME(msg), H_REQUIRE_BODY_NAME(msg), true,
          "aux_bool\\(\"%d\"\\) not met" },
       {  H_CHECK_STREQ_HEAD_NAME(msg), H_CHECK_STREQ_BODY_NAME(msg), false,
          "aux_str\\(\"%d\"\\) != \"\" \\(foo != \\)" },
       {  H_REQUIRE_STREQ_HEAD_NAME(msg), H_REQUIRE_STREQ_BODY_NAME(msg), true,
          "aux_str\\(\"%d\"\\) != \"\" \\(foo != \\)" },
       { NULL, NULL, false, NULL }
    };

    for (t = &tests[0]; t->head != NULL; t++) {
        printf("Checking with an expected '%s' message\n", t->msg);

        init_and_run_h_tc("h_check", t->head, t->body);

        if (t->fatal) {
            bool matched =
                atf_utils_grep_file(
                    "^failed: .*macros_test.c:[0-9]+: %s$", "result", t->msg);
            ATF_CHECK_MSG(matched, "couldn't find error string in result");
        } else {
            bool matched = atf_utils_grep_file("Check failed: .*"
                "macros_test.c:[0-9]+: %s$", "error", t->msg);
            ATF_CHECK_MSG(matched, "couldn't find error string in output");
        }
    }
}

/* ---------------------------------------------------------------------
 * Tests cases for the header file.
 * --------------------------------------------------------------------- */

BUILD_TC(use, "macros_h_test.c",
         "Tests that the macros provided by the atf-c/macros.h file "
         "do not cause syntax errors when used",
         "Build of macros_h_test.c failed; some macros in atf-c/macros.h "
         "are broken");

ATF_TC(detect_unused_tests);
ATF_TC_HEAD(detect_unused_tests, tc)
{
    atf_tc_set_md_var(tc, "descr",
                      "Tests that defining an unused test case raises a "
                      "warning (and thus an error)");
}
ATF_TC_BODY(detect_unused_tests, tc)
{
    const char* validate_compiler =
        "struct test_struct { int dummy; };\n"
        "#define define_unused static struct test_struct unused\n"
        "define_unused;\n";

    atf_utils_create_file("compiler_test.c", "%s", validate_compiler);
    if (build_check_c_o("compiler_test.c"))
        atf_tc_expect_fail("Compiler does not raise a warning on an unused "
                           "static global variable declared by a macro");

    if (build_check_c_o_srcdir(tc, "unused_test.c"))
        atf_tc_fail("Build of unused_test.c passed; unused test cases are "
                    "not properly detected");
}

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{
    ATF_TP_ADD_TC(tp, check);
    ATF_TP_ADD_TC(tp, check_eq);
    ATF_TP_ADD_TC(tp, check_streq);
    ATF_TP_ADD_TC(tp, check_errno);
    ATF_TP_ADD_TC(tp, check_match);

    ATF_TP_ADD_TC(tp, require);
    ATF_TP_ADD_TC(tp, require_eq);
    ATF_TP_ADD_TC(tp, require_streq);
    ATF_TP_ADD_TC(tp, require_errno);
    ATF_TP_ADD_TC(tp, require_match);

    ATF_TP_ADD_TC(tp, msg_embedded_fmt);

    /* Add the test cases for the header file. */
    ATF_TP_ADD_TC(tp, use);
    ATF_TP_ADD_TC(tp, detect_unused_tests);

    return atf_no_error();
}
