/* $NetBSD: t_ucontext.c,v 1.1 2011/10/15 06:54:52 jruoho Exp $ */

/*
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
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
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_ucontext.c,v 1.1 2011/10/15 06:54:52 jruoho Exp $");

#include <atf-c.h>
#include <stdio.h>
#include <ucontext.h>

ATF_TC(ucontext_basic);
ATF_TC_HEAD(ucontext_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks {get,set}context(2)");
}

ATF_TC_BODY(ucontext_basic, tc)
{
	ucontext_t u, v, w;
	volatile int x, y;

	x = 0;
	y = 0;

	printf("Start\n");

	getcontext(&u);
	y++;

	printf("x == %d\n", x);

	getcontext(&v);

	if ( x < 20 ) {
		x++;
		getcontext(&w);
		setcontext(&u);
	}

	printf("End, y = %d\n", y);
	ATF_REQUIRE_EQ(y, 21);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, ucontext_basic);

	return atf_no_error();
}
