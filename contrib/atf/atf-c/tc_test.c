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

#include "atf-c/tc.h"

#include <stdbool.h>
#include <string.h>

#include <atf-c.h>

#include "atf-c/detail/test_helpers.h"

/* ---------------------------------------------------------------------
 * Auxiliary test cases.
 * --------------------------------------------------------------------- */

ATF_TC_HEAD(empty, tc)
{
    if (tc != NULL) {}
}
ATF_TC_BODY(empty, tc)
{
}

ATF_TC_HEAD(test_var, tc)
{
    atf_tc_set_md_var(tc, "test-var", "Test text");
}

/* ---------------------------------------------------------------------
 * Test cases for the "atf_tc_t" type.
 * --------------------------------------------------------------------- */

ATF_TC(init);
ATF_TC_HEAD(init, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_tc_init function");
}
ATF_TC_BODY(init, tcin)
{
    atf_tc_t tc;

    RE(atf_tc_init(&tc, "test1", ATF_TC_HEAD_NAME(empty),
                   ATF_TC_BODY_NAME(empty), NULL, NULL));
    ATF_REQUIRE(strcmp(atf_tc_get_ident(&tc), "test1") == 0);
    ATF_REQUIRE(!atf_tc_has_md_var(&tc, "test-var"));
    atf_tc_fini(&tc);

    RE(atf_tc_init(&tc, "test2", ATF_TC_HEAD_NAME(test_var),
                   ATF_TC_BODY_NAME(empty), NULL, NULL));
    ATF_REQUIRE(strcmp(atf_tc_get_ident(&tc), "test2") == 0);
    ATF_REQUIRE(atf_tc_has_md_var(&tc, "test-var"));
    atf_tc_fini(&tc);
}

ATF_TC(init_pack);
ATF_TC_HEAD(init_pack, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_tc_init_pack function");
}
ATF_TC_BODY(init_pack, tcin)
{
    atf_tc_t tc;
    atf_tc_pack_t tcp1 = {
        .m_ident = "test1",
        .m_head = ATF_TC_HEAD_NAME(empty),
        .m_body = ATF_TC_BODY_NAME(empty),
        .m_cleanup = NULL,
    };
    atf_tc_pack_t tcp2 = {
        .m_ident = "test2",
        .m_head = ATF_TC_HEAD_NAME(test_var),
        .m_body = ATF_TC_BODY_NAME(empty),
        .m_cleanup = NULL,
    };

    RE(atf_tc_init_pack(&tc, &tcp1, NULL));
    ATF_REQUIRE(strcmp(atf_tc_get_ident(&tc), "test1") == 0);
    ATF_REQUIRE(!atf_tc_has_md_var(&tc, "test-var"));
    atf_tc_fini(&tc);

    RE(atf_tc_init_pack(&tc, &tcp2, NULL));
    ATF_REQUIRE(strcmp(atf_tc_get_ident(&tc), "test2") == 0);
    ATF_REQUIRE(atf_tc_has_md_var(&tc, "test-var"));
    atf_tc_fini(&tc);
}

ATF_TC(vars);
ATF_TC_HEAD(vars, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_tc_get_md_var, "
                      "atf_tc_has_md_var and atf_tc_set_md_var functions");
}
ATF_TC_BODY(vars, tcin)
{
    atf_tc_t tc;

    RE(atf_tc_init(&tc, "test1", ATF_TC_HEAD_NAME(empty),
                   ATF_TC_BODY_NAME(empty), NULL, NULL));
    ATF_REQUIRE(!atf_tc_has_md_var(&tc, "test-var"));
    RE(atf_tc_set_md_var(&tc, "test-var", "Test value"));
    ATF_REQUIRE(atf_tc_has_md_var(&tc, "test-var"));
    ATF_REQUIRE(strcmp(atf_tc_get_md_var(&tc, "test-var"), "Test value") == 0);
    atf_tc_fini(&tc);
}

ATF_TC(config);
ATF_TC_HEAD(config, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_tc_get_config_var, "
                      "atf_tc_get_config_var_wd and atf_tc_has_config_var "
                      "functions");
}
ATF_TC_BODY(config, tcin)
{
    atf_tc_t tc;
    const char *const config[] = { "test-var", "test-value", NULL };

    RE(atf_tc_init(&tc, "test1", ATF_TC_HEAD_NAME(empty),
                   ATF_TC_BODY_NAME(empty), NULL, NULL));
    ATF_REQUIRE(!atf_tc_has_config_var(&tc, "test-var"));
    ATF_REQUIRE(!atf_tc_has_md_var(&tc, "test-var"));
    atf_tc_fini(&tc);

    RE(atf_tc_init(&tc, "test1", ATF_TC_HEAD_NAME(empty),
                   ATF_TC_BODY_NAME(empty), NULL, config));
    ATF_REQUIRE(atf_tc_has_config_var(&tc, "test-var"));
    ATF_REQUIRE(strcmp(atf_tc_get_config_var(&tc, "test-var"),
                     "test-value") == 0);
    ATF_REQUIRE(!atf_tc_has_md_var(&tc, "test-var"));
    ATF_REQUIRE(!atf_tc_has_config_var(&tc, "test-var2"));
    ATF_REQUIRE(strcmp(atf_tc_get_config_var_wd(&tc, "test-var2", "def-value"),
                     "def-value") == 0);
    atf_tc_fini(&tc);
}

/* ---------------------------------------------------------------------
 * Test cases for the free functions.
 * --------------------------------------------------------------------- */

/* TODO: Add test cases for atf_tc_run.  This is going to be very tough,
 * but good tests here could allow us to avoid much of the indirect
 * testing done later on. */

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{
    /* Add the test cases for the "atf_tcr_t" type. */
    ATF_TP_ADD_TC(tp, init);
    ATF_TP_ADD_TC(tp, init_pack);
    ATF_TP_ADD_TC(tp, vars);
    ATF_TP_ADD_TC(tp, config);

    /* Add the test cases for the free functions. */
    /* TODO */

    return atf_no_error();
}
