/* Copyright (c) 2010 The NetBSD Foundation, Inc.
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

#include "atf-c/tp.h"

#include <string.h>
#include <unistd.h>

#include <atf-c.h>

ATF_TC(getopt);
ATF_TC_HEAD(getopt, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks if getopt(3) global state is "
        "reset by the test program driver so that test cases can use "
        "getopt(3) again");
}
ATF_TC_BODY(getopt, tc)
{
    /* Provide an option that is unknown to the test program driver and
     * one that is, together with an argument that would be swallowed by
     * the test program option if it were recognized. */
    int argc = 4;
    char arg1[] = "progname";
    char arg2[] = "-Z";
    char arg3[] = "-s";
    char arg4[] = "foo";
    char *const argv[] = { arg1, arg2, arg3, arg4, NULL };

    int ch;
    bool zflag;

    /* Given that this obviously is a test program, and that we used the
     * same driver to start, we can test getopt(3) right here without doing
     * any fancy stuff. */
    zflag = false;
    while ((ch = getopt(argc, argv, ":Z")) != -1) {
        switch (ch) {
        case 'Z':
            zflag = true;
            break;

        case '?':
        default:
            if (optopt != 's')
                atf_tc_fail("Unexpected unknown option -%c found", optopt);
        }
    }

    ATF_REQUIRE(zflag);
    ATF_REQUIRE_EQ_MSG(1, argc - optind, "Invalid number of arguments left "
        "after the call to getopt(3)");
    ATF_CHECK_STREQ_MSG("foo", argv[optind], "The non-option argument is "
        "invalid");
}

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{
    ATF_TP_ADD_TC(tp, getopt);

    return atf_no_error();
}
