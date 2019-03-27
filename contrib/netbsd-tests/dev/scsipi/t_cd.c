/*	$NetBSD: t_cd.c,v 1.8 2017/01/13 21:30:39 christos Exp $	*/

/*
 * Copyright (c) 2010 Antti Kantee.  All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/ioctl.h>

#include <atf-c.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <util.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "scsitest.h"

#include "h_macros.h"

ATF_TC(noisyeject);
ATF_TC_HEAD(noisyeject, tc)
{

	atf_tc_set_md_var(tc, "descr", "test for CD eject noisyness "
	    "(PR kern/43785)");
}

ATF_TC_BODY(noisyeject, tc)
{
	static char fname[] = "/dev/rcd0_";
	int part, fd, arg = 0;

	RL(part = getrawpartition());
	fname[strlen(fname)-1] = 'a' + part;
	rump_init();
	/*
	 * Rump CD emulation has been fixed, so no longer a problem.
	 *
	atf_tc_expect_signal(SIGSEGV, "PR kern/47646: Broken test or "
	    "a real problem in rump or the driver");
	 */
	RL(fd = rump_sys_open(fname, O_RDWR));
	RL(rump_sys_ioctl(fd, DIOCEJECT, &arg));

	ATF_REQUIRE_EQ(rump_scsitest_err[RUMP_SCSITEST_NOISYSYNC], 0);
	RL(rump_sys_close(fd));
	// atf_tc_expect_fail("PR kern/43785");
	ATF_REQUIRE_EQ(rump_scsitest_err[RUMP_SCSITEST_NOISYSYNC], 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, noisyeject);

	return atf_no_error();
}
