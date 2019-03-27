/* $NetBSD: t_rename.c,v 1.3 2017/01/13 21:30:41 christos Exp $ */

/*
 * Copyright (c) 2001, 2008, 2010 The NetBSD Foundation, Inc.
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
#define _NETBSD_SOURCE	/* strlcat/random */
#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008, 2010\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_rename.c,v 1.3 2017/01/13 21:30:41 christos Exp $");

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_macros.h"

ATF_TC(rename);
ATF_TC_HEAD(rename, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks rename(2)");
}
ATF_TC_BODY(rename, tc)
{
	struct stat sb;

	REQUIRE_LIBC(open("t1", O_CREAT | O_TRUNC | O_WRONLY, 0600), -1);
	REQUIRE_LIBC(link("t1", "t2"), -1);

	/* Check if rename to same name works as expected */
	REQUIRE_LIBC(rename("t1", "t1"), -1);

	/* Rename removed file? */
	REQUIRE_LIBC(stat("t1", &sb), -1);

	REQUIRE_LIBC(rename("t1", "t2"), -1);

#if BSD_RENAME
	/* check if rename of hardlinked file works the BSD way */
	ATF_REQUIRE_MSG(stat("t1", &sb) != 0, "BSD rename should remove file t1");
	ATF_REQUIRE_EQ(errno, ENOENT);
#else
	/* check if rename of hardlinked file works as the standard says */
	REQUIRE_LIBC(stat("t1", &sb), -1);
#endif
	/* check if we get the expected error */
	/* this also exercises icky shared libraries goo */
	ATF_REQUIRE_MSG(rename("no/such/file/or/dir", "no/such/file/or/dir") != 0,
		"No error renaming nonexistent file");
	ATF_REQUIRE_EQ(errno, ENOENT);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, rename);

	return atf_no_error();
}
