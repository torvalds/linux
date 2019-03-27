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

#include <atf-c/macros.h>

void atf_require_inside_if(void);
void atf_require_equal_inside_if(void);
void atf_check_errno_semicolons(void);
void atf_require_errno_semicolons(void);

void
atf_require_inside_if(void)
{
    /* Make sure that ATF_REQUIRE can be used inside an if statement that
     * does not have braces.  Earlier versions of it generated an error
     * if there was an else clause because they confused the compiler
     * by defining an unprotected nested if. */
    if (true)
        ATF_REQUIRE(true);
    else
        ATF_REQUIRE(true);
}

void
atf_require_equal_inside_if(void)
{
    /* Make sure that ATF_REQUIRE_EQUAL can be used inside an if statement
     * that does not have braces.  Earlier versions of it generated an
     * error if there was an else clause because they confused the
     * compiler by defining an unprotected nested if. */
    if (true)
        ATF_REQUIRE_EQ(true, true);
    else
        ATF_REQUIRE_EQ(true, true);
}

void
atf_check_errno_semicolons(void)
{
    /* Check that ATF_CHECK_ERRNO does not contain a semicolon that would
     * cause an empty-statement that confuses some compilers. */
    ATF_CHECK_ERRNO(1, 1 == 1);
    ATF_CHECK_ERRNO(2, 2 == 2);
}

void
atf_require_errno_semicolons(void)
{
    /* Check that ATF_REQUIRE_ERRNO does not contain a semicolon that would
     * cause an empty-statement that confuses some compilers. */
    ATF_REQUIRE_ERRNO(1, 1 == 1);
    ATF_REQUIRE_ERRNO(2, 2 == 2);
}

/* Test case names should not be expanded during instatiation so that they
 * can have the exact same name as macros. */
#define TEST_MACRO_1 invalid + name
#define TEST_MACRO_2 invalid + name
#define TEST_MACRO_3 invalid + name
ATF_TC(TEST_MACRO_1);
ATF_TC_HEAD(TEST_MACRO_1, tc) { if (tc != NULL) {} }
ATF_TC_BODY(TEST_MACRO_1, tc) { if (tc != NULL) {} }
atf_tc_t *test_name_1 = &ATF_TC_NAME(TEST_MACRO_1);
atf_tc_pack_t *test_pack_1 = &ATF_TC_PACK_NAME(TEST_MACRO_1);
void (*head_1)(atf_tc_t *) = ATF_TC_HEAD_NAME(TEST_MACRO_1);
void (*body_1)(const atf_tc_t *) = ATF_TC_BODY_NAME(TEST_MACRO_1);
ATF_TC_WITH_CLEANUP(TEST_MACRO_2);
ATF_TC_HEAD(TEST_MACRO_2, tc) { if (tc != NULL) {} }
ATF_TC_BODY(TEST_MACRO_2, tc) { if (tc != NULL) {} }
ATF_TC_CLEANUP(TEST_MACRO_2, tc) { if (tc != NULL) {} }
atf_tc_t *test_name_2 = &ATF_TC_NAME(TEST_MACRO_2);
atf_tc_pack_t *test_pack_2 = &ATF_TC_PACK_NAME(TEST_MACRO_2);
void (*head_2)(atf_tc_t *) = ATF_TC_HEAD_NAME(TEST_MACRO_2);
void (*body_2)(const atf_tc_t *) = ATF_TC_BODY_NAME(TEST_MACRO_2);
void (*cleanup_2)(const atf_tc_t *) = ATF_TC_CLEANUP_NAME(TEST_MACRO_2);
ATF_TC_WITHOUT_HEAD(TEST_MACRO_3);
ATF_TC_BODY(TEST_MACRO_3, tc) { if (tc != NULL) {} }
atf_tc_t *test_name_3 = &ATF_TC_NAME(TEST_MACRO_3);
atf_tc_pack_t *test_pack_3 = &ATF_TC_PACK_NAME(TEST_MACRO_3);
void (*body_3)(const atf_tc_t *) = ATF_TC_BODY_NAME(TEST_MACRO_3);
