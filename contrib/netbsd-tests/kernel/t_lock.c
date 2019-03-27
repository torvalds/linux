/* $NetBSD: t_lock.c,v 1.2 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2002, 2008 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_lock.c,v 1.2 2017/01/13 21:30:41 christos Exp $");

#include <sys/time.h>

#include <machine/lock.h>
#include <signal.h>

#include <atf-c.h>

#include "h_macros.h"

__cpu_simple_lock_t lk;
volatile int handled = 0;

static void
handler(int sig)
{
	handled = 1;
	__cpu_simple_unlock(&lk);
}

ATF_TC(lock);
ATF_TC_HEAD(lock, tc)
{
	atf_tc_set_md_var(tc, "timeout", "3");
	atf_tc_set_md_var(tc, "descr",
	    "Checks __cpu_simple_lock()/__cpu_simple_unlock()");
}
ATF_TC_BODY(lock, tc)
{
	struct itimerval itv;

	__cpu_simple_lock_init(&lk);

	REQUIRE_LIBC(signal(SIGVTALRM, handler), SIG_ERR);

	itv.it_interval.tv_sec = 0;
	itv.it_interval.tv_usec = 0;
	itv.it_value.tv_sec = 1;
	itv.it_value.tv_usec = 0;
	RL(setitimer(ITIMER_VIRTUAL, &itv, NULL));

	__cpu_simple_lock(&lk);
	__cpu_simple_lock(&lk);

	ATF_REQUIRE(handled);

	__cpu_simple_unlock(&lk);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, lock);

	return atf_no_error();
}
