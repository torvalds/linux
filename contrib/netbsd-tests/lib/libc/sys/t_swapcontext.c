/* $NetBSD: t_swapcontext.c,v 1.3 2013/05/05 10:28:11 skrll Exp $ */

/*
 * Copyright (c) 2012 Emmanuel Dreyfus. All rights reserved.
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
__RCSID("$NetBSD");

#include <ucontext.h>
#include <stdio.h>
#include <stdlib.h>
#include <lwp.h>

#include <atf-c.h>

#define STACKSIZE 65536

char stack[STACKSIZE];
ucontext_t nctx;
ucontext_t octx;
void *otls;
void *ntls;
int val1, val2;
int alter_tlsbase;

/* ARGSUSED0 */
static void
swapfunc(void *arg)
{
	ntls = _lwp_getprivate();
	printf("after swapcontext TLS pointer = %p\n", ntls);

	if (alter_tlsbase) {
		ATF_REQUIRE_EQ(ntls, &val1);
		printf("TLS pointer modified by swapcontext()\n");
	} else {
		ATF_REQUIRE_EQ(ntls, &val2);
		printf("TLS pointer left untouched by swapcontext()\n");
	}

	/* Go back in main */
	ATF_REQUIRE(swapcontext(&nctx, &octx));

	/* NOTREACHED */
	return;
}

static void
mainfunc(void)
{
	printf("Testing if swapcontext() alters TLS pointer if _UC_TLSBASE "
	       "is %s\n", (alter_tlsbase) ? "left set" : "cleared");

	_lwp_setprivate(&val1);
	printf("before swapcontext TLS pointer = %p\n", &val1);

	ATF_REQUIRE(getcontext(&nctx) == 0);

	nctx.uc_stack.ss_sp = stack;
	nctx.uc_stack.ss_size = sizeof(stack);

#ifndef _UC_TLSBASE
	ATF_REQUIRE_MSG(0, "_UC_TLSBASE is not defined");
#else /* _UC_TLSBASE */
	ATF_REQUIRE(nctx.uc_flags & _UC_TLSBASE);
	if (!alter_tlsbase)
		nctx.uc_flags &= ~_UC_TLSBASE;
#endif /* _UC_TLSBASE */

	makecontext(&nctx, swapfunc, 0);

	_lwp_setprivate(&val2);
	otls = _lwp_getprivate();
	printf("before swapcontext TLS pointer = %p\n", otls);
	ATF_REQUIRE(swapcontext(&octx, &nctx) == 0);

	printf("Test completed\n");
}


ATF_TC(swapcontext1);
ATF_TC_HEAD(swapcontext1, tc)
{
	atf_tc_set_md_var(tc, "descr", "Testing if swapcontext() can let "
	    "TLS pointer untouched");
}
ATF_TC_BODY(swapcontext1, tc)
{
	alter_tlsbase = 0;
	mainfunc();
}

ATF_TC(swapcontext2);
ATF_TC_HEAD(swapcontext2, tc)
{
	atf_tc_set_md_var(tc, "descr", "Testing if swapcontext() can "
	    "modify TLS pointer");
}
ATF_TC_BODY(swapcontext2, tc)
{
	alter_tlsbase = 1;
	mainfunc();
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, swapcontext1);
	ATF_TP_ADD_TC(tp, swapcontext2);

	return atf_no_error();
}
