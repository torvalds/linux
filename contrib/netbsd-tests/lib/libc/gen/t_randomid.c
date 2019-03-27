/* $NetBSD: t_randomid.c,v 1.5 2015/03/07 09:59:15 isaki Exp $ */

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

#include <atf-c.h>

#include <sys/types.h>

#include <assert.h>
#include <inttypes.h>
#include <randomid.h>
#include <stdio.h>
#include <string.h>

#define	PERIOD		30000

uint32_t last[65536];

ATF_TC(randomid_basic);
ATF_TC_HEAD(randomid_basic, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check randomid(3)");
}

ATF_TC_BODY(randomid_basic, tc)
{
	static randomid_t ctx = NULL;
	uint32_t lowest, n, diff;
	uint16_t id;

	memset(last, 0, sizeof(last));
	ctx = randomid_new(16, (long)3600);

	lowest = UINT32_MAX;

	for (n = 0; n < 100000; n++) {
		id = randomid(ctx);

		if (last[id] > 0) {
			diff = n - last[id];

			if (diff <= lowest) {
				if (lowest != UINT32_MAX)
					printf("id %5d: last call at %9"PRIu32
					    ", current call %9"PRIu32
					    " (diff %5"PRIu32"), "
					    "lowest %"PRIu32"\n",
					    id, last[id], n, diff, lowest);

				ATF_REQUIRE_MSG(diff >= PERIOD,
				    "diff (%"PRIu32") less than minimum "
				    "period (%d)", diff, PERIOD);

				lowest = diff;
			}
		}

		last[id] = n;
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, randomid_basic);

	return atf_no_error();
}
