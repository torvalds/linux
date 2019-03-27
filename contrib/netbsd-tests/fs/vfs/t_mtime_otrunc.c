/*-
 * Copyright (c) 2017 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2013\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_mtime_otrunc.c,v 1.1 2017/02/02 22:07:05 martin Exp $");

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <atf-c.h>

#include <rump/rump_syscalls.h>
#include <rump/rump.h>

#include "../common/h_fsmacros.h"

#define LOCKFILE	"lock"

static time_t
lock_it(void)
{
	struct stat st;

	if (rump_sys_stat(LOCKFILE, &st) != 0)
		st.st_mtime = 0;

	int f = rump_sys_open(LOCKFILE, O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if (f == -1) return 0;
	rump_sys_close(f);

	return st.st_mtime;
}

static void
otrunc_mtime_update(const atf_tc_t *tc, const char *path)
{
	time_t last_ts = 0;
	int res;

	/* atf_tc_expect_fail("PR kern/51762"); */

	res = rump_sys_chdir(path);
	if (res == -1)
		atf_tc_fail("chdir failed");

	for (int i = 0; i < 5; i++) {
		time_t l = lock_it();
		printf("last lock: %ld\n", (long)l);
		ATF_REQUIRE_MSG(i == 0 || l > last_ts,
		    "iteration %d: lock time did not increase, old time %lu, "
		    "new time %lu", i,
		    (unsigned long)last_ts, (unsigned long)l);
		last_ts = l;
		sleep(2);
	}
	rump_sys_chdir("/");
}

ATF_FSAPPLY(otrunc_mtime_update, "Checks for mtime updates by open(O_TRUNC) (PR kern/51762)");
