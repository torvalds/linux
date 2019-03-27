/* Copyright (c) 2007 The NetBSD Foundation, Inc.
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

#include "atf-c/detail/user.h"

#include <sys/param.h>
#include <sys/types.h>

#include <limits.h>
#include <stdio.h>
#include <unistd.h>

#include <atf-c.h>

#include "atf-c/detail/test_helpers.h"

/* ---------------------------------------------------------------------
 * Test cases for the free functions.
 * --------------------------------------------------------------------- */

ATF_TC(euid);
ATF_TC_HEAD(euid, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_user_euid function");
}
ATF_TC_BODY(euid, tc)
{
    ATF_REQUIRE_EQ(atf_user_euid(), geteuid());
}

ATF_TC(is_member_of_group);
ATF_TC_HEAD(is_member_of_group, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_user_is_member_of_group "
                      "function");
}
ATF_TC_BODY(is_member_of_group, tc)
{
    gid_t gids[NGROUPS_MAX];
    gid_t g, maxgid;
    int ngids;
    const gid_t maxgid_limit = 1 << 16;

    {
        int i;

        ngids = getgroups(NGROUPS_MAX, gids);
        if (ngids == -1)
            atf_tc_fail("Call to getgroups failed");
        maxgid = 0;
        for (i = 0; i < ngids; i++) {
            printf("User group %d is %u\n", i, gids[i]);
            if (maxgid < gids[i])
                maxgid = gids[i];
        }
        printf("User belongs to %d groups\n", ngids);
        printf("Last GID is %u\n", maxgid);
    }

    if (maxgid > maxgid_limit) {
        printf("Test truncated from %u groups to %u to keep the run time "
               "reasonable enough\n", maxgid, maxgid_limit);
        maxgid = maxgid_limit;
    }

    for (g = 0; g < maxgid; g++) {
        bool found = false;
        int i;

        for (i = 0; !found && i < ngids; i++) {
            if (gids[i] == g)
                found = true;
        }

        if (found) {
            printf("Checking if user belongs to group %d\n", g);
            ATF_REQUIRE(atf_user_is_member_of_group(g));
        } else {
            printf("Checking if user does not belong to group %d\n", g);
            ATF_REQUIRE(!atf_user_is_member_of_group(g));
        }
    }
}

ATF_TC(is_root);
ATF_TC_HEAD(is_root, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_user_is_root function");
}
ATF_TC_BODY(is_root, tc)
{
    if (geteuid() == 0)
        ATF_REQUIRE(atf_user_is_root());
    else
        ATF_REQUIRE(!atf_user_is_root());
}

ATF_TC(is_unprivileged);
ATF_TC_HEAD(is_unprivileged, tc)
{
    atf_tc_set_md_var(tc, "descr", "Tests the atf_user_is_unprivileged "
                      "function");
}
ATF_TC_BODY(is_unprivileged, tc)
{
    if (geteuid() != 0)
        ATF_REQUIRE(atf_user_is_unprivileged());
    else
        ATF_REQUIRE(!atf_user_is_unprivileged());
}

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{
    ATF_TP_ADD_TC(tp, euid);
    ATF_TP_ADD_TC(tp, is_member_of_group);
    ATF_TP_ADD_TC(tp, is_root);
    ATF_TP_ADD_TC(tp, is_unprivileged);

    return atf_no_error();
}
