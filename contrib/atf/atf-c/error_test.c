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

#include "atf-c/error.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <atf-c.h>

#include "atf-c/defs.h"

/* ---------------------------------------------------------------------
 * Auxiliary functions.
 * --------------------------------------------------------------------- */

static
void
test_format(const atf_error_t err ATF_DEFS_ATTRIBUTE_UNUSED,
            char *buf, size_t buflen)
{
    snprintf(buf, buflen, "Test formatting function");
}

/* ---------------------------------------------------------------------
 * Tests for the "atf_error" type.
 * --------------------------------------------------------------------- */

ATF_TC(error_new);
ATF_TC_HEAD(error_new, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the construction of an error "
                      "object");
}
ATF_TC_BODY(error_new, tc)
{
    atf_error_t err;
    int data;

    err = atf_error_new("test_error", NULL, 0, NULL);
    ATF_REQUIRE(atf_error_is(err, "test_error"));
    ATF_REQUIRE(!atf_error_is(err, "unknown_error"));
    ATF_REQUIRE(atf_error_data(err) == NULL);
    atf_error_free(err);

    data = 5;
    err = atf_error_new("test_data_error", &data, sizeof(data), NULL);
    ATF_REQUIRE(atf_error_is(err, "test_data_error"));
    ATF_REQUIRE(!atf_error_is(err, "unknown_error"));
    ATF_REQUIRE(atf_error_data(err) != NULL);
    ATF_REQUIRE_EQ(*((const int *)atf_error_data(err)), 5);
    atf_error_free(err);
}

ATF_TC(error_new_wo_memory);
ATF_TC_HEAD(error_new_wo_memory, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks that an unavailable memory error "
                      "raised when constructing an error object "
                            "is properly converted to the no_memory "
                            "static error type");
}
ATF_TC_BODY(error_new_wo_memory, tc)
{
    atf_error_t err;
    void *invalid;

    invalid = (void *)1;

    err = atf_error_new("test_error", invalid, SIZE_MAX, NULL);
    ATF_REQUIRE(atf_error_is(err, "no_memory"));
    ATF_REQUIRE(atf_error_data(err) == NULL);
    atf_error_free(err);
}

ATF_TC(no_error);
ATF_TC_HEAD(no_error, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks that constructing a non-error "
                      "object works");
}
ATF_TC_BODY(no_error, tc)
{
    atf_error_t err;

    err = atf_no_error();
    ATF_REQUIRE(!atf_is_error(err));
}

ATF_TC(is_error);
ATF_TC_HEAD(is_error, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the is_error method to determine "
                      "if an error object holds success or an error");
}
ATF_TC_BODY(is_error, tc)
{
    atf_error_t err;

    err = atf_no_error();
    ATF_REQUIRE(!atf_is_error(err));

    err = atf_error_new("test_error", NULL, 0, NULL);
    ATF_REQUIRE(atf_is_error(err));
    atf_error_free(err);
}

ATF_TC(format);
ATF_TC_HEAD(format, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the default formatting function "
                      "and the ability to change it");
}
ATF_TC_BODY(format, tc)
{
    atf_error_t err;
    char buf[1024];

    printf("Testing default formatting function\n");
    err = atf_error_new("test_error", NULL, 0, NULL);
    atf_error_format(err, buf, sizeof(buf));
    printf("Error string is: %s\n", buf);
    ATF_REQUIRE(strcmp(buf, "Error 'test_error'") == 0);
    atf_error_free(err);

    printf("Testing custom formatting function\n");
    err = atf_error_new("test_error", NULL, 0, test_format);
    atf_error_format(err, buf, sizeof(buf));
    printf("Error string is: %s\n", buf);
    ATF_REQUIRE(strcmp(buf, "Test formatting function") == 0);
    atf_error_free(err);
}

/* ---------------------------------------------------------------------
 * Tests for the "libc" error.
 * --------------------------------------------------------------------- */

ATF_TC(libc_new);
ATF_TC_HEAD(libc_new, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the construction of libc errors");
}
ATF_TC_BODY(libc_new, tc)
{
    atf_error_t err;

    err = atf_libc_error(ENOMEM, "Test message 1");
    ATF_REQUIRE(atf_error_is(err, "libc"));
    ATF_REQUIRE_EQ(atf_libc_error_code(err), ENOMEM);
    ATF_REQUIRE(strcmp(atf_libc_error_msg(err), "Test message 1") == 0);
    atf_error_free(err);

    err = atf_libc_error(EPERM, "%s message %d", "Test", 2);
    ATF_REQUIRE(atf_error_is(err, "libc"));
    ATF_REQUIRE_EQ(atf_libc_error_code(err), EPERM);
    ATF_REQUIRE(strcmp(atf_libc_error_msg(err), "Test message 2") == 0);
    atf_error_free(err);
}

ATF_TC(libc_format);
ATF_TC_HEAD(libc_format, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the formatting of libc errors");
}
ATF_TC_BODY(libc_format, tc)
{
    atf_error_t err;
    char buf[1024];

    err = atf_libc_error(ENOMEM, "Test message 1");
    atf_error_format(err, buf, sizeof(buf));
    ATF_REQUIRE(strstr(buf, strerror(ENOMEM)) != NULL);
    ATF_REQUIRE(strstr(buf, "Test message 1") != NULL);
    atf_error_free(err);

    err = atf_libc_error(EPERM, "Test message 2");
    atf_error_format(err, buf, sizeof(buf));
    ATF_REQUIRE(strstr(buf, strerror(EPERM)) != NULL);
    ATF_REQUIRE(strstr(buf, "Test message 2") != NULL);
    atf_error_free(err);

    err = atf_libc_error(EPERM, "%s message %d", "Test", 3);
    atf_error_format(err, buf, sizeof(buf));
    ATF_REQUIRE(strstr(buf, strerror(EPERM)) != NULL);
    ATF_REQUIRE(strstr(buf, "Test message 3") != NULL);
    atf_error_free(err);
}

/* ---------------------------------------------------------------------
 * Tests for the "no_memory" error.
 * --------------------------------------------------------------------- */

ATF_TC(no_memory_new);
ATF_TC_HEAD(no_memory_new, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the construction of no_memory "
                      "errors");
}
ATF_TC_BODY(no_memory_new, tc)
{
    atf_error_t err;

    err = atf_no_memory_error();
    ATF_REQUIRE(atf_error_is(err, "no_memory"));
    ATF_REQUIRE(atf_error_data(err) == NULL);
    atf_error_free(err);
}

ATF_TC(no_memory_format);
ATF_TC_HEAD(no_memory_format, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the formatting of no_memory "
                      "errors");
}
ATF_TC_BODY(no_memory_format, tc)
{
    atf_error_t err;
    char buf[1024];

    err = atf_no_memory_error();
    atf_error_format(err, buf, sizeof(buf));
    ATF_REQUIRE(strcmp(buf, "Not enough memory") == 0);
    atf_error_free(err);
}

ATF_TC(no_memory_twice);
ATF_TC_HEAD(no_memory_twice, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the construction of no_memory "
                      "errors multiple times, as this error is initialized "
                      "statically");
}
ATF_TC_BODY(no_memory_twice, tc)
{
    {
        atf_error_t err = atf_no_memory_error();
        ATF_REQUIRE(atf_error_is(err, "no_memory"));
        ATF_REQUIRE(atf_error_data(err) == NULL);
        atf_error_free(err);
    }

    {
        atf_error_t err = atf_no_memory_error();
        ATF_REQUIRE(atf_error_is(err, "no_memory"));
        ATF_REQUIRE(atf_error_data(err) == NULL);
        atf_error_free(err);
    }
}

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{
    /* Add the tests for the "atf_error" type. */
    ATF_TP_ADD_TC(tp, error_new);
    ATF_TP_ADD_TC(tp, error_new_wo_memory);
    ATF_TP_ADD_TC(tp, no_error);
    ATF_TP_ADD_TC(tp, is_error);
    ATF_TP_ADD_TC(tp, format);

    /* Add the tests for the "libc" error. */
    ATF_TP_ADD_TC(tp, libc_new);
    ATF_TP_ADD_TC(tp, libc_format);

    /* Add the tests for the "no_memory" error. */
    ATF_TP_ADD_TC(tp, no_memory_new);
    ATF_TP_ADD_TC(tp, no_memory_format);
    ATF_TP_ADD_TC(tp, no_memory_twice);

    return atf_no_error();
}
