/* Copyright (c) 2009 The NetBSD Foundation, Inc.
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

#include "atf-c/build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

#include "atf-c/detail/env.h"
#include "atf-c/detail/test_helpers.h"
#include "atf-c/h_build.h"
#include "atf-c/utils.h"

/* ---------------------------------------------------------------------
 * Auxiliary functions.
 * --------------------------------------------------------------------- */

static
bool
equal_arrays(const char *const *exp_array, char **actual_array)
{
    bool equal;

    if (*exp_array == NULL && *actual_array == NULL)
        equal = true;
    else if (*exp_array == NULL || *actual_array == NULL)
        equal = false;
    else {
        equal = true;
        while (*actual_array != NULL) {
            if (*exp_array == NULL || strcmp(*exp_array, *actual_array) != 0) {
                equal = false;
                break;
            }
            exp_array++;
            actual_array++;
        }
    }

    return equal;
}

static
void
check_equal_array(const char *const *exp_array, char **actual_array)
{
    {
        const char *const *exp_ptr;
        printf("Expected arguments:");
        for (exp_ptr = exp_array; *exp_ptr != NULL; exp_ptr++)
            printf(" '%s'", *exp_ptr);
        printf("\n");
    }

    {
        char **actual_ptr;
        printf("Returned arguments:");
        for (actual_ptr = actual_array; *actual_ptr != NULL; actual_ptr++)
            printf(" '%s'", *actual_ptr);
        printf("\n");
    }

    if (!equal_arrays(exp_array, actual_array))
        atf_tc_fail_nonfatal("The constructed argv differs from the "
                             "expected values");
}

static
void
verbose_set_env(const char *var, const char *val)
{
    printf("Setting %s to '%s'\n", var, val);
    RE(atf_env_set(var, val));
}

/* ---------------------------------------------------------------------
 * Internal test cases.
 * --------------------------------------------------------------------- */

ATF_TC(equal_arrays);
ATF_TC_HEAD(equal_arrays, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the test case internal "
                      "equal_arrays function");
}
ATF_TC_BODY(equal_arrays, tc)
{
    {
        const char *const exp[] = { NULL };
        char *actual[] = { NULL };

        ATF_CHECK(equal_arrays(exp, actual));
    }

    {
        const char *const exp[] = { NULL };
        char *actual[2] = { strdup("foo"), NULL };

        ATF_CHECK(!equal_arrays(exp, actual));
        free(actual[0]);
    }

    {
        const char *const exp[] = { "foo", NULL };
        char *actual[] = { NULL };

        ATF_CHECK(!equal_arrays(exp, actual));
    }

    {
        const char *const exp[] = { "foo", NULL };
        char *actual[2] = { strdup("foo"), NULL };

        ATF_CHECK(equal_arrays(exp, actual));
        free(actual[0]);
    }
}

/* ---------------------------------------------------------------------
 * Test cases for the free functions.
 * --------------------------------------------------------------------- */

ATF_TC(c_o);
ATF_TC_HEAD(c_o, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_build_c_o function");
}
ATF_TC_BODY(c_o, tc)
{
    struct c_o_test *test;

    for (test = c_o_tests; test->expargv[0] != NULL; test++) {
        printf("> Test: %s\n", test->msg);

        verbose_set_env("ATF_BUILD_CC", test->cc);
        verbose_set_env("ATF_BUILD_CFLAGS", test->cflags);
        verbose_set_env("ATF_BUILD_CPPFLAGS", test->cppflags);

        {
            char **argv;
            if (test->hasoptargs)
                RE(atf_build_c_o(test->sfile, test->ofile, test->optargs,
                                 &argv));
            else
                RE(atf_build_c_o(test->sfile, test->ofile, NULL, &argv));
            check_equal_array(test->expargv, argv);
            atf_utils_free_charpp(argv);
        }
    }
}

ATF_TC(cpp);
ATF_TC_HEAD(cpp, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_build_cpp function");
}
ATF_TC_BODY(cpp, tc)
{
    struct cpp_test *test;

    for (test = cpp_tests; test->expargv[0] != NULL; test++) {
        printf("> Test: %s\n", test->msg);

        verbose_set_env("ATF_BUILD_CPP", test->cpp);
        verbose_set_env("ATF_BUILD_CPPFLAGS", test->cppflags);

        {
            char **argv;
            if (test->hasoptargs)
                RE(atf_build_cpp(test->sfile, test->ofile, test->optargs,
                                 &argv));
            else
                RE(atf_build_cpp(test->sfile, test->ofile, NULL, &argv));
            check_equal_array(test->expargv, argv);
            atf_utils_free_charpp(argv);
        }
    }
}

ATF_TC(cxx_o);
ATF_TC_HEAD(cxx_o, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_build_cxx_o function");
}
ATF_TC_BODY(cxx_o, tc)
{
    struct cxx_o_test *test;

    for (test = cxx_o_tests; test->expargv[0] != NULL; test++) {
        printf("> Test: %s\n", test->msg);

        verbose_set_env("ATF_BUILD_CXX", test->cxx);
        verbose_set_env("ATF_BUILD_CXXFLAGS", test->cxxflags);
        verbose_set_env("ATF_BUILD_CPPFLAGS", test->cppflags);

        {
            char **argv;
            if (test->hasoptargs)
                RE(atf_build_cxx_o(test->sfile, test->ofile, test->optargs,
                                   &argv));
            else
                RE(atf_build_cxx_o(test->sfile, test->ofile, NULL, &argv));
            check_equal_array(test->expargv, argv);
            atf_utils_free_charpp(argv);
        }
    }
}

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{
    /* Add the internal test cases. */
    ATF_TP_ADD_TC(tp, equal_arrays);

    /* Add the test cases for the free functions. */
    ATF_TP_ADD_TC(tp, c_o);
    ATF_TP_ADD_TC(tp, cpp);
    ATF_TP_ADD_TC(tp, cxx_o);

    return atf_no_error();
}
