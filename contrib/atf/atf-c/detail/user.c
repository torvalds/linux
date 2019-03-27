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
#include <unistd.h>

#include "atf-c/detail/sanity.h"

/* ---------------------------------------------------------------------
 * Free functions.
 * --------------------------------------------------------------------- */

uid_t
atf_user_euid(void)
{
    return geteuid();
}

bool
atf_user_is_member_of_group(gid_t gid)
{
    static gid_t groups[NGROUPS_MAX];
    static int ngroups = -1;
    bool found;
    int i;

    if (ngroups == -1) {
        ngroups = getgroups(NGROUPS_MAX, groups);
        INV(ngroups >= 0);
    }

    found = false;
    for (i = 0; !found && i < ngroups; i++)
        if (groups[i] == gid)
            found = true;
    return found;
}

bool
atf_user_is_root(void)
{
    return geteuid() == 0;
}

bool
atf_user_is_unprivileged(void)
{
    return geteuid() != 0;
}
