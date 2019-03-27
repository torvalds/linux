/*	$NetBSD: t_modlinkset.c,v 1.3 2017/01/13 21:30:43 christos Exp $	*/

/*
 * Copyright (c) 2009 The NetBSD Foundation, Inc.
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

#include <rump/rump.h>
#include <rump/ukfs.h>

#include <atf-c.h>
#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <string.h>

#include "h_macros.h"

ATF_TC(modlinkset);
ATF_TC_HEAD(modlinkset, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that module linkset bootstrap "
	    "works");
}

/*
 * We link against cd9660 and msdosfs (both chosed because the names
 * are unlikely to ever be a substring of a another file system).
 * Without proper linkset handling at most one will be reported.
 */
ATF_TC_BODY(modlinkset, tc)
{
	char buf[1024];

	rump_init();
	if (ukfs_vfstypes(buf, sizeof(buf)) == -1)
		atf_tc_fail_errno("ukfs_vfstypes");

	ATF_CHECK((strstr(buf, "msdos") != NULL));
	ATF_CHECK((strstr(buf, "cd9660") != NULL));
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, modlinkset);

	return atf_no_error();
}
