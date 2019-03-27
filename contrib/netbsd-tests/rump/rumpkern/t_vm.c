/*	$NetBSD: t_vm.c,v 1.4 2017/01/13 21:30:43 christos Exp $	*/

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
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

#include <atf-c.h>
#include <stdio.h>
#include <stdlib.h>

#include "h_macros.h"
#include "../kernspace/kernspace.h"

ATF_TC(busypage);
ATF_TC_HEAD(busypage, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks VM pagewaits work");
}

ATF_TC_BODY(busypage, tc)
{

	rump_init();

	rump_schedule();
	rumptest_busypage();
	rump_unschedule();
}

ATF_TC(uvmwait);
ATF_TC_HEAD(uvmwait, tc)
{

	atf_tc_set_md_var(tc, "descr", "Tests that uvm_wait works");
	atf_tc_set_md_var(tc, "timeout", "30");
}

#define UVMWAIT_LIMIT 1024*1024
ATF_TC_BODY(uvmwait, tc)
{
	char buf[64];

	/* limit rump kernel memory */
	snprintf(buf, sizeof(buf), "%d", UVMWAIT_LIMIT);
	setenv("RUMP_MEMLIMIT", buf, 1);

	rump_init();

	rump_schedule();
	rumptest_alloc(UVMWAIT_LIMIT);
	rump_unschedule();
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, busypage);
	ATF_TP_ADD_TC(tp, uvmwait);

	return atf_no_error();
}
