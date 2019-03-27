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

#if !defined(ATF_C_MACROS_H)
#define ATF_C_MACROS_H

#include <string.h>

#include <atf-c/defs.h>
#include <atf-c/error.h>
#include <atf-c/tc.h>
#include <atf-c/tp.h>
#include <atf-c/utils.h>

#define ATF_TC_NAME(tc) \
    (atfu_ ## tc ## _tc)

#define ATF_TC_PACK_NAME(tc) \
    (atfu_ ## tc ## _tc_pack)

#define ATF_TC_WITHOUT_HEAD(tc) \
    static void atfu_ ## tc ## _body(const atf_tc_t *); \
    static atf_tc_t atfu_ ## tc ## _tc; \
    static atf_tc_pack_t atfu_ ## tc ## _tc_pack = { \
        .m_ident = #tc, \
        .m_head = NULL, \
        .m_body = atfu_ ## tc ## _body, \
        .m_cleanup = NULL, \
    }

#define ATF_TC(tc) \
    static void atfu_ ## tc ## _head(atf_tc_t *); \
    static void atfu_ ## tc ## _body(const atf_tc_t *); \
    static atf_tc_t atfu_ ## tc ## _tc; \
    static atf_tc_pack_t atfu_ ## tc ## _tc_pack = { \
        .m_ident = #tc, \
        .m_head = atfu_ ## tc ## _head, \
        .m_body = atfu_ ## tc ## _body, \
        .m_cleanup = NULL, \
    }

#define ATF_TC_WITH_CLEANUP(tc) \
    static void atfu_ ## tc ## _head(atf_tc_t *); \
    static void atfu_ ## tc ## _body(const atf_tc_t *); \
    static void atfu_ ## tc ## _cleanup(const atf_tc_t *); \
    static atf_tc_t atfu_ ## tc ## _tc; \
    static atf_tc_pack_t atfu_ ## tc ## _tc_pack = { \
        .m_ident = #tc, \
        .m_head = atfu_ ## tc ## _head, \
        .m_body = atfu_ ## tc ## _body, \
        .m_cleanup = atfu_ ## tc ## _cleanup, \
    }

#define ATF_TC_HEAD(tc, tcptr) \
    static \
    void \
    atfu_ ## tc ## _head(atf_tc_t *tcptr ATF_DEFS_ATTRIBUTE_UNUSED)

#define ATF_TC_HEAD_NAME(tc) \
    (atfu_ ## tc ## _head)

#define ATF_TC_BODY(tc, tcptr) \
    static \
    void \
    atfu_ ## tc ## _body(const atf_tc_t *tcptr ATF_DEFS_ATTRIBUTE_UNUSED)

#define ATF_TC_BODY_NAME(tc) \
    (atfu_ ## tc ## _body)

#define ATF_TC_CLEANUP(tc, tcptr) \
    static \
    void \
    atfu_ ## tc ## _cleanup(const atf_tc_t *tcptr ATF_DEFS_ATTRIBUTE_UNUSED)

#define ATF_TC_CLEANUP_NAME(tc) \
    (atfu_ ## tc ## _cleanup)

#define ATF_TP_ADD_TCS(tps) \
    static atf_error_t atfu_tp_add_tcs(atf_tp_t *); \
    int atf_tp_main(int, char **, atf_error_t (*)(atf_tp_t *)); \
    \
    int \
    main(int argc, char **argv) \
    { \
        return atf_tp_main(argc, argv, atfu_tp_add_tcs); \
    } \
    static \
    atf_error_t \
    atfu_tp_add_tcs(atf_tp_t *tps)

#define ATF_TP_ADD_TC(tp, tc) \
    do { \
        atf_error_t atfu_err; \
        char **atfu_config = atf_tp_get_config(tp); \
        if (atfu_config == NULL) \
            return atf_no_memory_error(); \
        atfu_err = atf_tc_init_pack(&atfu_ ## tc ## _tc, \
                                    &atfu_ ## tc ## _tc_pack, \
                                    (const char *const *)atfu_config); \
        atf_utils_free_charpp(atfu_config); \
        if (atf_is_error(atfu_err)) \
            return atfu_err; \
        atfu_err = atf_tp_add_tc(tp, &atfu_ ## tc ## _tc); \
        if (atf_is_error(atfu_err)) \
            return atfu_err; \
    } while (0)

#define ATF_REQUIRE_MSG(expression, fmt, ...) \
    do { \
        if (!(expression)) \
            atf_tc_fail_requirement(__FILE__, __LINE__, fmt, ##__VA_ARGS__); \
    } while(0)

#define ATF_CHECK_MSG(expression, fmt, ...) \
    do { \
        if (!(expression)) \
            atf_tc_fail_check(__FILE__, __LINE__, fmt, ##__VA_ARGS__); \
    } while(0)

#define ATF_REQUIRE(expression) \
    do { \
        if (!(expression)) \
            atf_tc_fail_requirement(__FILE__, __LINE__, "%s", \
                                    #expression " not met"); \
    } while(0)

#define ATF_CHECK(expression) \
    do { \
        if (!(expression)) \
            atf_tc_fail_check(__FILE__, __LINE__, "%s", \
                              #expression " not met"); \
    } while(0)

#define ATF_REQUIRE_EQ(expected, actual) \
    ATF_REQUIRE_MSG((expected) == (actual), "%s != %s", #expected, #actual)

#define ATF_CHECK_EQ(expected, actual) \
    ATF_CHECK_MSG((expected) == (actual), "%s != %s", #expected, #actual)

#define ATF_REQUIRE_EQ_MSG(expected, actual, fmt, ...) \
    ATF_REQUIRE_MSG((expected) == (actual), "%s != %s: " fmt, \
                    #expected, #actual, ##__VA_ARGS__)

#define ATF_CHECK_EQ_MSG(expected, actual, fmt, ...) \
    ATF_CHECK_MSG((expected) == (actual), "%s != %s: " fmt, \
                  #expected, #actual, ##__VA_ARGS__)

#define ATF_REQUIRE_STREQ(expected, actual) \
    ATF_REQUIRE_MSG(strcmp(expected, actual) == 0, "%s != %s (%s != %s)", \
                    #expected, #actual, expected, actual)

#define ATF_CHECK_STREQ(expected, actual) \
    ATF_CHECK_MSG(strcmp(expected, actual) == 0, "%s != %s (%s != %s)", \
                  #expected, #actual, expected, actual)

#define ATF_REQUIRE_STREQ_MSG(expected, actual, fmt, ...) \
    ATF_REQUIRE_MSG(strcmp(expected, actual) == 0, \
                    "%s != %s (%s != %s): " fmt, \
                    #expected, #actual, expected, actual, ##__VA_ARGS__)

#define ATF_CHECK_STREQ_MSG(expected, actual, fmt, ...) \
    ATF_CHECK_MSG(strcmp(expected, actual) == 0, \
                  "%s != %s (%s != %s): " fmt, \
                  #expected, #actual, expected, actual, ##__VA_ARGS__)

#define ATF_REQUIRE_MATCH(regexp, string) \
    ATF_REQUIRE_MSG(atf_utils_grep_string("%s", string, regexp), \
                    "'%s' not matched in '%s'", regexp, string);

#define ATF_CHECK_MATCH(regexp, string) \
    ATF_CHECK_MSG(atf_utils_grep_string("%s", string, regexp), \
                  "'%s' not matched in '%s'", regexp, string);

#define ATF_REQUIRE_MATCH_MSG(regexp, string, fmt, ...) \
    ATF_REQUIRE_MSG(atf_utils_grep_string("%s", string, regexp), \
                    "'%s' not matched in '%s': " fmt, regexp, string, \
                    ##__VA_ARGS__);

#define ATF_CHECK_MATCH_MSG(regexp, string, fmt, ...) \
    ATF_CHECK_MSG(atf_utils_grep_string("%s", string, regexp), \
                  "'%s' not matched in '%s': " fmt, regexp, string, \
                  ##__VA_ARGS__);

#define ATF_CHECK_ERRNO(exp_errno, bool_expr) \
    atf_tc_check_errno(__FILE__, __LINE__, exp_errno, #bool_expr, bool_expr)

#define ATF_REQUIRE_ERRNO(exp_errno, bool_expr) \
    atf_tc_require_errno(__FILE__, __LINE__, exp_errno, #bool_expr, bool_expr)

#endif /* !defined(ATF_C_MACROS_H) */
