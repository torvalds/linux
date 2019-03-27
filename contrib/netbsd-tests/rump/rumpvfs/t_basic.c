/*	$NetBSD: t_basic.c,v 1.3 2017/01/13 21:30:43 christos Exp $	*/

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/mount.h>
#include <sys/sysctl.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <atf-c.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "h_macros.h"

ATF_TC(lseekrv);
ATF_TC_HEAD(lseekrv, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test lseek return values");
}

#define TESTFILE "testi"

#define FIVE_MEGS (5*1024*1024)
#define FIVE_GIGS (5*1024*1024*1024LL)

ATF_TC_BODY(lseekrv, tc)
{
	off_t rv;
	int fd;

	RZ(rump_init());
	RL(fd = rump_sys_open(TESTFILE, O_RDWR | O_CREAT, 0777));

	rv = rump_sys_lseek(37, FIVE_MEGS, SEEK_SET);
	ATF_REQUIRE_ERRNO(EBADF, rv == -1);

	rv = rump_sys_lseek(fd, FIVE_MEGS, SEEK_SET);
	ATF_REQUIRE_EQ(rv, FIVE_MEGS);

	rv = rump_sys_lseek(fd, FIVE_GIGS, SEEK_SET);
	ATF_REQUIRE_EQ(rv, FIVE_GIGS);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, lseekrv);

	return atf_no_error();
}
