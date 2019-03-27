/*	$NetBSD: t_dummy.c,v 1.6 2016/11/19 15:13:46 kamil Exp $	*/

/*-
 * Copyright (c) 2016 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_dummy.c,v 1.6 2016/11/19 15:13:46 kamil Exp $");

#include "h_common.h"
#include <pthread_dbg.h>
#include <stdio.h>

#include <atf-c.h>


ATF_TC(dummy1);
ATF_TC_HEAD(dummy1, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Asserts that dummy lookup functions stop td_open() with failure");
}

ATF_TC_BODY(dummy1, tc)
{

	struct td_proc_callbacks_t dummy_callbacks;
	td_proc_t *main_ta;

	dummy_callbacks.proc_read	= dummy_proc_read;
	dummy_callbacks.proc_write	= dummy_proc_write;
	dummy_callbacks.proc_lookup	= dummy_proc_lookup;
	dummy_callbacks.proc_regsize	= dummy_proc_regsize;
	dummy_callbacks.proc_getregs	= dummy_proc_getregs;
	dummy_callbacks.proc_setregs	= dummy_proc_setregs;

	ATF_REQUIRE(td_open(&dummy_callbacks, NULL, &main_ta) == TD_ERR_ERR);
}

ATF_TC(dummy2);
ATF_TC_HEAD(dummy2, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Asserts that td_open() for basic proc_{read,write,lookup} works");
}

ATF_TC_BODY(dummy2, tc)
{
	struct td_proc_callbacks_t dummy_callbacks;
	td_proc_t *main_ta;

	dummy_callbacks.proc_read	= basic_proc_read;
	dummy_callbacks.proc_write	= basic_proc_write;
	dummy_callbacks.proc_lookup	= basic_proc_lookup;
	dummy_callbacks.proc_regsize	= dummy_proc_regsize;
	dummy_callbacks.proc_getregs	= dummy_proc_getregs;
	dummy_callbacks.proc_setregs	= dummy_proc_setregs;

	printf("Calling td_open(3)\n");
	ATF_REQUIRE(td_open(&dummy_callbacks, NULL, &main_ta) == TD_ERR_OK);

	printf("Calling td_close(3)\n");
	ATF_REQUIRE(td_close(main_ta) == TD_ERR_OK);
}

ATF_TC(dummy3);
ATF_TC_HEAD(dummy3, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Asserts that calling twice td_open() for the same process fails");
}

ATF_TC_BODY(dummy3, tc)
{
	struct td_proc_callbacks_t dummy_callbacks;
	td_proc_t *main_ta1;
	td_proc_t *main_ta2;

	dummy_callbacks.proc_read	= basic_proc_read;
	dummy_callbacks.proc_write	= basic_proc_write;
	dummy_callbacks.proc_lookup	= basic_proc_lookup;
	dummy_callbacks.proc_regsize	= dummy_proc_regsize;
	dummy_callbacks.proc_getregs	= dummy_proc_getregs;
	dummy_callbacks.proc_setregs	= dummy_proc_setregs;

	printf("Calling td_open(3) for the first time - expecting success\n");
	ATF_REQUIRE(td_open(&dummy_callbacks, NULL, &main_ta1) == TD_ERR_OK);

	printf("Calling td_open(3) for the first time - expecting in-use\n");
	ATF_REQUIRE(td_open(&dummy_callbacks, NULL, &main_ta2) ==
	    TD_ERR_INUSE);

	printf("Calling td_close(3) for the first successful call\n");
	ATF_REQUIRE(td_close(main_ta1) == TD_ERR_OK);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, dummy1);
	ATF_TP_ADD_TC(tp, dummy2);
	ATF_TP_ADD_TC(tp, dummy3);

	return atf_no_error();
}
