/*	$NetBSD: t_rnd.c,v 1.10 2017/01/13 21:30:41 christos Exp $	*/

/*
 * Copyright (c) 2009 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_rnd.c,v 1.10 2017/01/13 21:30:41 christos Exp $");

#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/rndio.h>

#include <atf-c.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include "h_macros.h"

ATF_TC(RNDADDDATA);
ATF_TC_HEAD(RNDADDDATA, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Checks ioctl(RNDADDDATA) (PR kern/42020)");
}

/* Adapted from example provided by Juho Salminen in the noted PR. */
ATF_TC_BODY(RNDADDDATA, tc)
{
	rnddata_t rd;
	int fd;

	rump_init();
	fd = rump_sys_open("/dev/random", O_RDWR, 0);
	if (fd == -1)
		atf_tc_fail_errno("cannot open /dev/random");

	rd.entropy = 1;
	rd.len = 1;
	if (rump_sys_ioctl(fd, RNDADDDATA, &rd) == -1)
		atf_tc_fail_errno("RNDADDDATA");
}

ATF_TC(RNDADDDATA2);
ATF_TC_HEAD(RNDADDDATA2, tc)
{
	atf_tc_set_md_var(tc, "descr", "checks ioctl(RNDADDDATA) deals with "
	    "garbage len field");
}
ATF_TC_BODY(RNDADDDATA2, tc)
{
	rnddata_t rd;
	int fd;

	rump_init();
	fd = rump_sys_open("/dev/random", O_RDWR, 0);
	if (fd == -1)
		atf_tc_fail_errno("cannot open /dev/random");
		
	rd.entropy = 1;
	rd.len = -1;
	ATF_REQUIRE_ERRNO(EINVAL, rump_sys_ioctl(fd, RNDADDDATA, &rd) == -1);
}

ATF_TC(read_random);
ATF_TC_HEAD(read_random, tc)
{
	atf_tc_set_md_var(tc, "descr", "does reading /dev/random return "
	    "within reasonable time");
	atf_tc_set_md_var(tc, "timeout", "10");
}

ATF_TC_BODY(read_random, tc)
{
	char buf[128];
	int fd;

	rump_init();
	RL(fd = rump_sys_open("/dev/random", O_RDONLY));
	RL(rump_sys_read(fd, buf, sizeof(buf)));
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, RNDADDDATA);
	ATF_TP_ADD_TC(tp, RNDADDDATA2);
	ATF_TP_ADD_TC(tp, read_random);

	return atf_no_error();
}
