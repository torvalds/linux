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

#include "atf-c/detail/dynstr.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

#include "atf-c/detail/test_helpers.h"

/* ---------------------------------------------------------------------
 * Tests for the "atf_dynstr" type.
 * --------------------------------------------------------------------- */

/*
 * Constructors and destructors.
 */

ATF_TC(init);
ATF_TC_HEAD(init, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the empty constructor");
}
ATF_TC_BODY(init, tc)
{
    atf_dynstr_t str;

    RE(atf_dynstr_init(&str));
    ATF_REQUIRE_EQ(atf_dynstr_length(&str), 0);
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "") == 0);
    atf_dynstr_fini(&str);
}

static
void
init_fmt(atf_dynstr_t *str, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    RE(atf_dynstr_init_ap(str, fmt, ap));
    va_end(ap);
}

ATF_TC(init_ap);
ATF_TC_HEAD(init_ap, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the formatted constructor using "
                      "a va_list argument");
}
ATF_TC_BODY(init_ap, tc)
{
    atf_dynstr_t str;

    init_fmt(&str, "String 1");
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "String 1") == 0);
    atf_dynstr_fini(&str);

    init_fmt(&str, "String %d", 2);
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "String 2") == 0);
    atf_dynstr_fini(&str);

    init_fmt(&str, "%s %d", "String", 3);
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "String 3") == 0);
    atf_dynstr_fini(&str);

    init_fmt(&str, "%s%s%s%s%s%s%s", "This ", "should ", "be ", "a ",
             "large ", "string ", "aaaabbbbccccdddd");
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str),
                     "This should be a large string "
                     "aaaabbbbccccdddd") == 0);
    atf_dynstr_fini(&str);
}

ATF_TC(init_fmt);
ATF_TC_HEAD(init_fmt, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the formatted constructor using "
                      "a variable list of parameters");
}
ATF_TC_BODY(init_fmt, tc)
{
    atf_dynstr_t str;

    RE(atf_dynstr_init_fmt(&str, "String 1"));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "String 1") == 0);
    atf_dynstr_fini(&str);

    RE(atf_dynstr_init_fmt(&str, "String %d", 2));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "String 2") == 0);
    atf_dynstr_fini(&str);

    RE(atf_dynstr_init_fmt(&str, "%s %d", "String", 3));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "String 3") == 0);
    atf_dynstr_fini(&str);

    RE(atf_dynstr_init_fmt(&str, "%s%s%s%s%s%s%s", "This ", "should ",
                           "be ", "a ", "large ", "string ",
                           "aaaabbbbccccdddd"));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str),
                     "This should be a large string "
                     "aaaabbbbccccdddd") == 0);
    atf_dynstr_fini(&str);
}

ATF_TC(init_raw);
ATF_TC_HEAD(init_raw, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the construction of a string "
                      "using a raw memory pointer");
}
ATF_TC_BODY(init_raw, tc)
{
    const char *src = "String 1, String 2";
    atf_dynstr_t str;

    RE(atf_dynstr_init_raw(&str, src, 0));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "") == 0);
    atf_dynstr_fini(&str);

    RE(atf_dynstr_init_raw(&str, src, 8));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "String 1") == 0);
    atf_dynstr_fini(&str);

    RE(atf_dynstr_init_raw(&str, src + 10, 8));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "String 2") == 0);
    atf_dynstr_fini(&str);

    RE(atf_dynstr_init_raw(&str, "String\0Lost", 11));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "String") == 0);
    atf_dynstr_fini(&str);

    {
        atf_error_t err = atf_dynstr_init_raw(&str, "NULL", SIZE_MAX - 1);
        ATF_REQUIRE(atf_is_error(err));
        ATF_REQUIRE(atf_error_is(err, "no_memory"));
        atf_error_free(err);
    }
}

ATF_TC(init_rep);
ATF_TC_HEAD(init_rep, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the construction of a string by "
                      "repeating characters");
}
ATF_TC_BODY(init_rep, tc)
{
    const size_t maxlen = 8192;
    char buf[maxlen + 1];
    size_t i;

    buf[0] = '\0';

    for (i = 0; i < maxlen; i++) {
        atf_dynstr_t str;

        RE(atf_dynstr_init_rep(&str, i, 'a'));

        if (strcmp(atf_dynstr_cstring(&str), buf) != 0) {
            fprintf(stderr, "Failed at iteration %zd\n", i);
            atf_tc_fail("Failed to construct dynstr by repeating %zd "
                        "times the '%c' character", i, 'a');
        }

        atf_dynstr_fini(&str);

        strcat(buf, "a");
    }

    {
        atf_dynstr_t str;
        atf_error_t err;

        err = atf_dynstr_init_rep(&str, SIZE_MAX, 'a');
        ATF_REQUIRE(atf_is_error(err));
        ATF_REQUIRE(atf_error_is(err, "no_memory"));
        atf_error_free(err);

        err = atf_dynstr_init_rep(&str, SIZE_MAX - 1, 'a');
        ATF_REQUIRE(atf_is_error(err));
        ATF_REQUIRE(atf_error_is(err, "no_memory"));
        atf_error_free(err);
    }
}

ATF_TC(init_substr);
ATF_TC_HEAD(init_substr, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the construction of a string "
                      "using a substring of another one");
}
ATF_TC_BODY(init_substr, tc)
{
    atf_dynstr_t src;
    atf_dynstr_t str;

    RE(atf_dynstr_init_fmt(&src, "Str 1, Str 2"));

    RE(atf_dynstr_init_substr(&str, &src, 0, 0));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "") == 0);
    atf_dynstr_fini(&str);

    RE(atf_dynstr_init_substr(&str, &src, 0, atf_dynstr_npos));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "Str 1, Str 2") == 0);
    atf_dynstr_fini(&str);

    RE(atf_dynstr_init_substr(&str, &src, 0, 100));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "Str 1, Str 2") == 0);
    atf_dynstr_fini(&str);

    RE(atf_dynstr_init_substr(&str, &src, 0, 5));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "Str 1") == 0);
    atf_dynstr_fini(&str);

    RE(atf_dynstr_init_substr(&str, &src, 100, atf_dynstr_npos));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "") == 0);
    atf_dynstr_fini(&str);

    RE(atf_dynstr_init_substr(&str, &src, 7, atf_dynstr_npos));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "Str 2") == 0);
    atf_dynstr_fini(&str);

    atf_dynstr_fini(&src);
}

ATF_TC(copy);
ATF_TC_HEAD(copy, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_dynstr_copy constructor");
}
ATF_TC_BODY(copy, tc)
{
    atf_dynstr_t str, str2;

    RE(atf_dynstr_init_fmt(&str, "Test string"));
    RE(atf_dynstr_copy(&str2, &str));

    ATF_REQUIRE(atf_equal_dynstr_dynstr(&str, &str2));

    RE(atf_dynstr_append_fmt(&str2, " non-shared text"));

    ATF_REQUIRE(!atf_equal_dynstr_dynstr(&str, &str2));

    atf_dynstr_fini(&str2);
    atf_dynstr_fini(&str);
}

ATF_TC(fini_disown);
ATF_TC_HEAD(fini_disown, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks grabbing ownership of the "
                      "internal plain C string");
}
ATF_TC_BODY(fini_disown, tc)
{
    const char *cstr;
    char *cstr2;
    atf_dynstr_t str;

    RE(atf_dynstr_init_fmt(&str, "Test string 1"));
    cstr = atf_dynstr_cstring(&str);
    cstr2 = atf_dynstr_fini_disown(&str);

    ATF_REQUIRE_EQ(cstr, cstr2);
    free(cstr2);
}

/*
 * Getters.
 */

ATF_TC(cstring);
ATF_TC_HEAD(cstring, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the method to obtain a plain C "
                      "string");
}
ATF_TC_BODY(cstring, tc)
{
    const char *cstr;
    atf_dynstr_t str;

    RE(atf_dynstr_init_fmt(&str, "Test string 1"));
    cstr = atf_dynstr_cstring(&str);
    ATF_REQUIRE(cstr != NULL);
    ATF_REQUIRE(strcmp(cstr, "Test string 1") == 0);
    atf_dynstr_fini(&str);

    RE(atf_dynstr_init_fmt(&str, "Test string 2"));
    cstr = atf_dynstr_cstring(&str);
    ATF_REQUIRE(cstr != NULL);
    ATF_REQUIRE(strcmp(cstr, "Test string 2") == 0);
    atf_dynstr_fini(&str);
}

ATF_TC(length);
ATF_TC_HEAD(length, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the method to obtain the length");
}
ATF_TC_BODY(length, tc)
{
    size_t i;

    for (i = 0; i < 8192; i++) {
        atf_dynstr_t str;
        RE(atf_dynstr_init_rep(&str, i, 'a'));
        ATF_REQUIRE_EQ(atf_dynstr_length(&str), i);
        atf_dynstr_fini(&str);
    }
}

ATF_TC(rfind_ch);
ATF_TC_HEAD(rfind_ch, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the method to locate the first "
                      "occurrence of a character starting from the end");
}
ATF_TC_BODY(rfind_ch, tc)
{
    atf_dynstr_t str;

    RE(atf_dynstr_init_fmt(&str, "Foo1/Bar2/,.Baz"));

    ATF_REQUIRE_EQ(atf_dynstr_rfind_ch(&str, '\0'), atf_dynstr_npos);

    ATF_REQUIRE_EQ(atf_dynstr_rfind_ch(&str, '0'), atf_dynstr_npos);
    ATF_REQUIRE_EQ(atf_dynstr_rfind_ch(&str, 'b'), atf_dynstr_npos);

    ATF_REQUIRE_EQ(atf_dynstr_rfind_ch(&str, 'F'), 0);
    ATF_REQUIRE_EQ(atf_dynstr_rfind_ch(&str, '/'), 9);
    ATF_REQUIRE_EQ(atf_dynstr_rfind_ch(&str, 'a'), 13);
    ATF_REQUIRE_EQ(atf_dynstr_rfind_ch(&str, 'z'), 14);

    atf_dynstr_fini(&str);
}

/*
 * Modifiers.
 */

static
void
check_append(atf_error_t (*append)(atf_dynstr_t *, const char *, ...))
{
    const size_t maxlen = 8192;
    char buf[maxlen + 1];
    size_t i;
    atf_dynstr_t str;

    printf("Appending with plain string\n");
    buf[0] = '\0';
    RE(atf_dynstr_init(&str));
    for (i = 0; i < maxlen; i++) {
        if (strcmp(atf_dynstr_cstring(&str), buf) != 0) {
            fprintf(stderr, "Failed at iteration %zd\n", i);
            atf_tc_fail("Failed to append character at iteration %zd", i);
        }

        RE(append(&str, "a"));
        strcat(buf, "a");
    }
    atf_dynstr_fini(&str);

    printf("Appending with formatted string\n");
    buf[0] = '\0';
    RE(atf_dynstr_init(&str));
    for (i = 0; i < maxlen; i++) {
        if (strcmp(atf_dynstr_cstring(&str), buf) != 0) {
            fprintf(stderr, "Failed at iteration %zd\n", i);
            atf_tc_fail("Failed to append character at iteration %zd", i);
        }

        RE(append(&str, "%s", "a"));
        strcat(buf, "a");
    }
    atf_dynstr_fini(&str);
}

static
atf_error_t
append_ap_aux(atf_dynstr_t *str, const char *fmt, ...)
{
    va_list ap;
    atf_error_t err;

    va_start(ap, fmt);
    err = atf_dynstr_append_ap(str, fmt, ap);
    va_end(ap);

    return err;
}

ATF_TC(append_ap);
ATF_TC_HEAD(append_ap, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks that appending a string to "
                      "another one works");
}
ATF_TC_BODY(append_ap, tc)
{
    check_append(append_ap_aux);
}

ATF_TC(append_fmt);
ATF_TC_HEAD(append_fmt, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks that appending a string to "
                      "another one works");
}
ATF_TC_BODY(append_fmt, tc)
{
    check_append(atf_dynstr_append_fmt);
}

ATF_TC(clear);
ATF_TC_HEAD(clear, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks clearing a string");
}
ATF_TC_BODY(clear, tc)
{
    atf_dynstr_t str;

    printf("Clear an empty string\n");
    RE(atf_dynstr_init(&str));
    atf_dynstr_clear(&str);
    ATF_REQUIRE_EQ(atf_dynstr_length(&str), 0);
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "") == 0);
    atf_dynstr_fini(&str);

    printf("Clear a non-empty string\n");
    RE(atf_dynstr_init_fmt(&str, "Not empty"));
    ATF_REQUIRE_EQ(atf_dynstr_length(&str), strlen("Not empty"));
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "Not empty") == 0);
    atf_dynstr_clear(&str);
    ATF_REQUIRE_EQ(atf_dynstr_length(&str), 0);
    ATF_REQUIRE(strcmp(atf_dynstr_cstring(&str), "") == 0);
    atf_dynstr_fini(&str);
}

static
void
check_prepend(atf_error_t (*prepend)(atf_dynstr_t *, const char *, ...))
{
    const size_t maxlen = 8192;
    char buf[maxlen + 1];
    size_t i;
    atf_dynstr_t str;

    printf("Prepending with plain string\n");
    buf[0] = '\0';
    RE(atf_dynstr_init(&str));
    for (i = 0; i < maxlen; i++) {
        if (strcmp(atf_dynstr_cstring(&str), buf) != 0) {
            fprintf(stderr, "Failed at iteration %zd\n", i);
            atf_tc_fail("Failed to prepend character at iteration %zd", i);
        }

        memmove(buf + 1, buf, i + 1);
        if (i % 2 == 0) {
            RE(prepend(&str, "%s", "a"));
            buf[0] = 'a';
        } else {
            RE(prepend(&str, "%s", "b"));
            buf[0] = 'b';
        }
    }
    atf_dynstr_fini(&str);

    printf("Prepending with formatted string\n");
    buf[0] = '\0';
    RE(atf_dynstr_init(&str));
    for (i = 0; i < maxlen; i++) {
        if (strcmp(atf_dynstr_cstring(&str), buf) != 0) {
            fprintf(stderr, "Failed at iteration %zd\n", i);
            atf_tc_fail("Failed to prepend character at iteration %zd", i);
        }

        memmove(buf + 1, buf, i + 1);
        if (i % 2 == 0) {
            RE(prepend(&str, "%s", "a"));
            buf[0] = 'a';
        } else {
            RE(prepend(&str, "%s", "b"));
            buf[0] = 'b';
        }
    }
    atf_dynstr_fini(&str);
}

static
atf_error_t
prepend_ap_aux(atf_dynstr_t *str, const char *fmt, ...)
{
    va_list ap;
    atf_error_t err;

    va_start(ap, fmt);
    err = atf_dynstr_prepend_ap(str, fmt, ap);
    va_end(ap);

    return err;
}

ATF_TC(prepend_ap);
ATF_TC_HEAD(prepend_ap, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks that prepending a string to "
                      "another one works");
}
ATF_TC_BODY(prepend_ap, tc)
{
    check_prepend(prepend_ap_aux);
}

ATF_TC(prepend_fmt);
ATF_TC_HEAD(prepend_fmt, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks that prepending a string to "
                      "another one works");
}
ATF_TC_BODY(prepend_fmt, tc)
{
    check_prepend(atf_dynstr_prepend_fmt);
}

/*
 * Operators.
 */

ATF_TC(equal_cstring);
ATF_TC_HEAD(equal_cstring, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_equal_dynstr_cstring "
                      "function");
}
ATF_TC_BODY(equal_cstring, tc)
{
    atf_dynstr_t str;

    RE(atf_dynstr_init(&str));
    ATF_REQUIRE( atf_equal_dynstr_cstring(&str, ""));
    ATF_REQUIRE(!atf_equal_dynstr_cstring(&str, "Test"));
    atf_dynstr_fini(&str);

    RE(atf_dynstr_init_fmt(&str, "Test"));
    ATF_REQUIRE( atf_equal_dynstr_cstring(&str, "Test"));
    ATF_REQUIRE(!atf_equal_dynstr_cstring(&str, ""));
    ATF_REQUIRE(!atf_equal_dynstr_cstring(&str, "Tes"));
    ATF_REQUIRE(!atf_equal_dynstr_cstring(&str, "Test "));
    atf_dynstr_fini(&str);
}

ATF_TC(equal_dynstr);
ATF_TC_HEAD(equal_dynstr, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_equal_dynstr_dynstr "
                      "function");
}
ATF_TC_BODY(equal_dynstr, tc)
{
    atf_dynstr_t str, str2;

    RE(atf_dynstr_init(&str));
    RE(atf_dynstr_init_fmt(&str2, "Test"));
    ATF_REQUIRE( atf_equal_dynstr_dynstr(&str, &str));
    ATF_REQUIRE(!atf_equal_dynstr_dynstr(&str, &str2));
    atf_dynstr_fini(&str2);
    atf_dynstr_fini(&str);
}

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{
    /* Constructors and destructors. */
    ATF_TP_ADD_TC(tp, init);
    ATF_TP_ADD_TC(tp, init_ap);
    ATF_TP_ADD_TC(tp, init_fmt);
    ATF_TP_ADD_TC(tp, init_raw);
    ATF_TP_ADD_TC(tp, init_rep);
    ATF_TP_ADD_TC(tp, init_substr);
    ATF_TP_ADD_TC(tp, copy);
    ATF_TP_ADD_TC(tp, fini_disown);

    /* Getters. */
    ATF_TP_ADD_TC(tp, cstring);
    ATF_TP_ADD_TC(tp, length);
    ATF_TP_ADD_TC(tp, rfind_ch);

    /* Modifiers. */
    ATF_TP_ADD_TC(tp, append_ap);
    ATF_TP_ADD_TC(tp, append_fmt);
    ATF_TP_ADD_TC(tp, clear);
    ATF_TP_ADD_TC(tp, prepend_ap);
    ATF_TP_ADD_TC(tp, prepend_fmt);

    /* Operators. */
    ATF_TP_ADD_TC(tp, equal_cstring);
    ATF_TP_ADD_TC(tp, equal_dynstr);

    return atf_no_error();
}
