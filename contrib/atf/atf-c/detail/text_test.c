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

#include "atf-c/detail/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atf-c.h>

#include "atf-c/detail/sanity.h"
#include "atf-c/detail/test_helpers.h"

/* ---------------------------------------------------------------------
 * Auxiliary functions.
 * --------------------------------------------------------------------- */

#define REQUIRE_ERROR(exp) \
    do { \
        atf_error_t err = exp; \
        ATF_REQUIRE(atf_is_error(err)); \
        atf_error_free(err); \
    } while (0)

static
size_t
array_size(const char *words[])
{
    size_t count;
    const char **word;

    count = 0;
    for (word = words; *word != NULL; word++)
        count++;

    return count;
}

static
void
check_split(const char *str, const char *delim, const char *words[])
{
    atf_list_t list;
    const char **word;
    size_t i;

    printf("Splitting '%s' with delimiter '%s'\n", str, delim);
    CE(atf_text_split(str, delim, &list));

    printf("Expecting %zd words\n", array_size(words));
    ATF_CHECK_EQ(atf_list_size(&list), array_size(words));

    for (word = words, i = 0; *word != NULL; word++, i++) {
        printf("Word at position %zd should be '%s'\n", i, words[i]);
        ATF_CHECK_STREQ((const char *)atf_list_index_c(&list, i), words[i]);
    }

    atf_list_fini(&list);
}

static
atf_error_t
word_acum(const char *word, void *data)
{
    char *acum = data;

    strcat(acum, word);

    return atf_no_error();
}

static
atf_error_t
word_count(const char *word ATF_DEFS_ATTRIBUTE_UNUSED, void *data)
{
    size_t *counter = data;

    (*counter)++;

    return atf_no_error();
}

struct fail_at {
    int failpos;
    int curpos;
};

static
atf_error_t
word_fail_at(const char *word ATF_DEFS_ATTRIBUTE_UNUSED, void *data)
{
    struct fail_at *fa = data;
    atf_error_t err;

    if (fa->failpos == fa->curpos)
        err = atf_no_memory_error(); /* Just a random error. */
    else {
        fa->curpos++;
        err = atf_no_error();
    }

    return err;
}

/* ---------------------------------------------------------------------
 * Test cases for the free functions.
 * --------------------------------------------------------------------- */

ATF_TC(for_each_word);
ATF_TC_HEAD(for_each_word, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_text_for_each_word"
                      "function");
}
ATF_TC_BODY(for_each_word, tc)
{
    size_t cnt;
    char acum[1024];

    cnt = 0;
    strcpy(acum, "");
    RE(atf_text_for_each_word("1 2 3", " ", word_count, &cnt));
    RE(atf_text_for_each_word("1 2 3", " ", word_acum, acum));
    ATF_REQUIRE(cnt == 3);
    ATF_REQUIRE(strcmp(acum, "123") == 0);

    cnt = 0;
    strcpy(acum, "");
    RE(atf_text_for_each_word("1 2 3", ".", word_count, &cnt));
    RE(atf_text_for_each_word("1 2 3", ".", word_acum, acum));
    ATF_REQUIRE(cnt == 1);
    ATF_REQUIRE(strcmp(acum, "1 2 3") == 0);

    cnt = 0;
    strcpy(acum, "");
    RE(atf_text_for_each_word("1 2 3 4 5", " ", word_count, &cnt));
    RE(atf_text_for_each_word("1 2 3 4 5", " ", word_acum, acum));
    ATF_REQUIRE(cnt == 5);
    ATF_REQUIRE(strcmp(acum, "12345") == 0);

    cnt = 0;
    strcpy(acum, "");
    RE(atf_text_for_each_word("1 2.3.4 5", " .", word_count, &cnt));
    RE(atf_text_for_each_word("1 2.3.4 5", " .", word_acum, acum));
    ATF_REQUIRE(cnt == 5);
    ATF_REQUIRE(strcmp(acum, "12345") == 0);

    {
        struct fail_at fa;
        fa.failpos = 3;
        fa.curpos = 0;
        atf_error_t err = atf_text_for_each_word("a b c d e", " ",
                                                 word_fail_at, &fa);
        ATF_REQUIRE(atf_is_error(err));
        ATF_REQUIRE(atf_error_is(err, "no_memory"));
        ATF_REQUIRE(fa.curpos == 3);
        atf_error_free(err);
    }
}

ATF_TC(format);
ATF_TC_HEAD(format, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the construction of free-form "
                      "strings using a variable parameters list");
}
ATF_TC_BODY(format, tc)
{
    char *str;
    atf_error_t err;

    err = atf_text_format(&str, "%s %s %d", "Test", "string", 1);
    ATF_REQUIRE(!atf_is_error(err));
    ATF_REQUIRE(strcmp(str, "Test string 1") == 0);
    free(str);
}

static
void
format_ap(char **dest, const char *fmt, ...)
{
    va_list ap;
    atf_error_t err;

    va_start(ap, fmt);
    err = atf_text_format_ap(dest, fmt, ap);
    va_end(ap);

    ATF_REQUIRE(!atf_is_error(err));
}

ATF_TC(format_ap);
ATF_TC_HEAD(format_ap, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the construction of free-form "
                      "strings using a va_list argument");
}
ATF_TC_BODY(format_ap, tc)
{
    char *str;

    format_ap(&str, "%s %s %d", "Test", "string", 1);
    ATF_REQUIRE(strcmp(str, "Test string 1") == 0);
    free(str);
}

ATF_TC(split);
ATF_TC_HEAD(split, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the split function");
}
ATF_TC_BODY(split, tc)
{
    {
        const char *words[] = { NULL };
        check_split("", " ", words);
    }

    {
        const char *words[] = { NULL };
        check_split(" ", " ", words);
    }

    {
        const char *words[] = { NULL };
        check_split("    ", " ", words);
    }

    {
        const char *words[] = { "a", "b", NULL };
        check_split("a b", " ", words);
    }

    {
        const char *words[] = { "a", "b", "c", "d", NULL };
        check_split("a b c d", " ", words);
    }

    {
        const char *words[] = { "foo", "bar", NULL };
        check_split("foo bar", " ", words);
    }

    {
        const char *words[] = { "foo", "bar", "baz", "foobar", NULL };
        check_split("foo bar baz foobar", " ", words);
    }

    {
        const char *words[] = { "foo", "bar", NULL };
        check_split(" foo bar", " ", words);
    }

    {
        const char *words[] = { "foo", "bar", NULL };
        check_split("foo  bar", " ", words);
    }

    {
        const char *words[] = { "foo", "bar", NULL };
        check_split("foo bar ", " ", words);
    }

    {
        const char *words[] = { "foo", "bar", NULL };
        check_split("  foo  bar  ", " ", words);
    }
}

ATF_TC(split_delims);
ATF_TC_HEAD(split_delims, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the split function using "
                      "different delimiters");
}
ATF_TC_BODY(split_delims, tc)
{

    {
        const char *words[] = { NULL };
        check_split("", "/", words);
    }

    {
        const char *words[] = { " ", NULL };
        check_split(" ", "/", words);
    }

    {
        const char *words[] = { "    ", NULL };
        check_split("    ", "/", words);
    }

    {
        const char *words[] = { "a", "b", NULL };
        check_split("a/b", "/", words);
    }

    {
        const char *words[] = { "a", "bcd", "ef", NULL };
        check_split("aLONGDELIMbcdLONGDELIMef", "LONGDELIM", words);
    }
}

ATF_TC(to_bool);
ATF_TC_HEAD(to_bool, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_text_to_bool function");
}
ATF_TC_BODY(to_bool, tc)
{
    bool b;

    RE(atf_text_to_bool("true", &b)); ATF_REQUIRE(b);
    RE(atf_text_to_bool("TRUE", &b)); ATF_REQUIRE(b);
    RE(atf_text_to_bool("yes", &b)); ATF_REQUIRE(b);
    RE(atf_text_to_bool("YES", &b)); ATF_REQUIRE(b);

    RE(atf_text_to_bool("false", &b)); ATF_REQUIRE(!b);
    RE(atf_text_to_bool("FALSE", &b)); ATF_REQUIRE(!b);
    RE(atf_text_to_bool("no", &b)); ATF_REQUIRE(!b);
    RE(atf_text_to_bool("NO", &b)); ATF_REQUIRE(!b);

    b = false;
    REQUIRE_ERROR(atf_text_to_bool("", &b));
    ATF_REQUIRE(!b);
    b = true;
    REQUIRE_ERROR(atf_text_to_bool("", &b));
    ATF_REQUIRE(b);

    b = false;
    REQUIRE_ERROR(atf_text_to_bool("tru", &b));
    ATF_REQUIRE(!b);
    b = true;
    REQUIRE_ERROR(atf_text_to_bool("tru", &b));
    ATF_REQUIRE(b);

    b = false;
    REQUIRE_ERROR(atf_text_to_bool("true2", &b));
    ATF_REQUIRE(!b);
    b = true;
    REQUIRE_ERROR(atf_text_to_bool("true2", &b));
    ATF_REQUIRE(b);

    b = false;
    REQUIRE_ERROR(atf_text_to_bool("fals", &b));
    ATF_REQUIRE(!b);
    b = true;
    REQUIRE_ERROR(atf_text_to_bool("fals", &b));
    ATF_REQUIRE(b);

    b = false;
    REQUIRE_ERROR(atf_text_to_bool("false2", &b));
    ATF_REQUIRE(!b);
    b = true;
    REQUIRE_ERROR(atf_text_to_bool("false2", &b));
    ATF_REQUIRE(b);
}

ATF_TC(to_long);
ATF_TC_HEAD(to_long, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_text_to_long function");
}
ATF_TC_BODY(to_long, tc)
{
    long l;

    RE(atf_text_to_long("0", &l)); ATF_REQUIRE_EQ(l, 0);
    RE(atf_text_to_long("-5", &l)); ATF_REQUIRE_EQ(l, -5);
    RE(atf_text_to_long("5", &l)); ATF_REQUIRE_EQ(l, 5);
    RE(atf_text_to_long("123456789", &l)); ATF_REQUIRE_EQ(l, 123456789);

    l = 1212;
    REQUIRE_ERROR(atf_text_to_long("", &l));
    ATF_REQUIRE_EQ(l, 1212);
    REQUIRE_ERROR(atf_text_to_long("foo", &l));
    ATF_REQUIRE_EQ(l, 1212);
    REQUIRE_ERROR(atf_text_to_long("1234x", &l));
    ATF_REQUIRE_EQ(l, 1212);
}

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{
    ATF_TP_ADD_TC(tp, for_each_word);
    ATF_TP_ADD_TC(tp, format);
    ATF_TP_ADD_TC(tp, format_ap);
    ATF_TP_ADD_TC(tp, split);
    ATF_TP_ADD_TC(tp, split_delims);
    ATF_TP_ADD_TC(tp, to_bool);
    ATF_TP_ADD_TC(tp, to_long);

    return atf_no_error();
}
