/* $NetBSD: t_ioctl.c,v 1.3 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2002, 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn.
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
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_ioctl.c,v 1.3 2017/01/13 21:30:41 christos Exp $");

#include <sys/event.h>
#include <sys/ioctl.h>

#include <stdio.h>
#include <string.h>

#include <atf-c.h>

#include "h_macros.h"

ATF_TC(kfilter_byfilter);
ATF_TC_HEAD(kfilter_byfilter, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks KFILTER_BYFILTER ioctl");
}
ATF_TC_BODY(kfilter_byfilter, tc)
{
	char buf[32];
	struct kfilter_mapping km;
	int kq;
	uint32_t i;

	RL(kq = kqueue());

	km.name = buf;
	km.len = sizeof(buf) - 1;

	for (i = 0; i < 7; ++i) {
		km.filter = i;
		RL(ioctl(kq, KFILTER_BYFILTER, &km));
		(void)printf("  map %d -> %s\n", km.filter, km.name);
	}

	km.filter = 7;
	ATF_REQUIRE_EQ(ioctl(kq, KFILTER_BYFILTER, &km), -1);
}

ATF_TC(kfilter_byname);
ATF_TC_HEAD(kfilter_byname, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks KFILTER_BYNAME ioctl");
}
ATF_TC_BODY(kfilter_byname, tc)
{
	const char *tests[] = {
		"EVFILT_READ",
		"EVFILT_WRITE",
		"EVFILT_AIO",
		"EVFILT_VNODE",
		"EVFILT_PROC",
		"EVFILT_SIGNAL",
		"EVFILT_TIMER",
		NULL
	};
	char buf[32];
	struct kfilter_mapping km;
	const char **test;
	int kq;

	RL(kq = kqueue());

	km.name = buf;

	for (test = &tests[0]; *test != NULL; ++test) {
		(void)strlcpy(buf, *test, sizeof(buf));
		RL(ioctl(kq, KFILTER_BYNAME, &km));
		(void)printf("  map %s -> %d\n", km.name, km.filter);
	}

	(void)strlcpy(buf, "NOTREG_FILTER", sizeof(buf));
	ATF_REQUIRE_EQ(ioctl(kq, KFILTER_BYNAME, &km), -1);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, kfilter_byfilter);
	ATF_TP_ADD_TC(tp, kfilter_byname);

	return atf_no_error();
}
