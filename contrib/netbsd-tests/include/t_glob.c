/*	$NetBSD: t_glob.c,v 1.1 2011/04/10 08:35:48 jruoho Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
__RCSID("$NetBSD: t_glob.c,v 1.1 2011/04/10 08:35:48 jruoho Exp $");

#include <atf-c.h>
#include <glob.h>
#include <string.h>

ATF_TC(glob_types);
ATF_TC_HEAD(glob_types, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test glob(3) types");
}

ATF_TC_BODY(glob_types, tc)
{
	glob_t g;

	/*
	 * IEEE Std 1003.1-2008:
	 *
	 * "The <glob.h> header shall define the glob_t structure type,
	 *  which shall include at least the following members:
	 *
	 *    size_t   gl_pathc Count of paths matched by pattern.
	 *    char   **gl_pathv Pointer to a list of matched pathnames.
	 *    size_t   gl_offs  Slots to reserve at the beginning of gl_pathv."
	 *
	 * Verify that gl_pathc and gl_offs are unsigned; PR standards/21401.
	 */
	(void)memset(&g, 0, sizeof(glob_t));

	g.gl_offs = g.gl_offs - 1;
	g.gl_pathc = g.gl_pathc - 1;

	ATF_REQUIRE(g.gl_pathc > 0);
	ATF_REQUIRE(g.gl_offs > 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, glob_types);

	return atf_no_error();
}
