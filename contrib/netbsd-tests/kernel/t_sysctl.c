/* $NetBSD: t_sysctl.c,v 1.1 2014/08/09 07:04:03 gson Exp $ */

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
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
__COPYRIGHT("@(#) Copyright (c) 2014\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_sysctl.c,v 1.1 2014/08/09 07:04:03 gson Exp $");

#include <sys/sysctl.h>
#include <errno.h>
#include <memory.h>

#include <atf-c.h>

ATF_TC(bufsize);
ATF_TC_HEAD(bufsize, tc)
{
	atf_tc_set_md_var(tc, "descr",
	        "Test sysctl integer reads with different buffer sizes");
}
ATF_TC_BODY(bufsize, tc)
{
	union {
		int int_val;
		unsigned char space[256];
	} buf;
	size_t len;
	for (len = 0; len < sizeof(buf); len++) {
		size_t oldlen = len;
		int r;
		memset(&buf, 0xFF, sizeof(buf));
		r = sysctlbyname("kern.job_control", &buf, &oldlen, 0, (size_t) 0);
		if (len < sizeof(int)) {
			ATF_REQUIRE_EQ(r, -1);
			ATF_REQUIRE_EQ(errno, ENOMEM);
		} else {
			ATF_REQUIRE_EQ(r, 0);
			ATF_REQUIRE_EQ(buf.int_val, 1);
			ATF_REQUIRE_EQ(oldlen, sizeof(int));
		}
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, bufsize);

	return atf_no_error();
}
