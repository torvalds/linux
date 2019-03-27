/*	$NetBSD: t_p2kifs.c,v 1.6 2017/01/13 21:30:43 christos Exp $	*/

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
#include <sys/sysctl.h>

#include <rump/rump.h>
#include <rump/rumpvnode_if.h>
#include <rump/rump_syscalls.h>

#include <atf-c.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "h_macros.h"

ATF_TC(makecn);
ATF_TC_HEAD(makecn, tc)
{

	atf_tc_set_md_var(tc, "descr", "Tests makecn/LOOKUP/freecn");
}

#define TESTFILE "testi"

ATF_TC_BODY(makecn, tc)
{
	struct componentname *cn;
	char pathstr[MAXPATHLEN] = TESTFILE;
	struct vnode *vp;
	extern struct vnode *rumpns_rootvnode;

	rump_init();

	/*
	 * Strategy is to create a componentname, edit the passed
	 * string, and then do a lookup with the componentname.
	 */
	RL(rump_sys_mkdir("/" TESTFILE, 0777));

	/* need stable lwp for componentname */
	RZ(rump_pub_lwproc_rfork(RUMP_RFCFDG));

	/* try it once with the right path */
	cn = rump_pub_makecn(RUMP_NAMEI_LOOKUP, 0, pathstr, strlen(pathstr),
	    rump_pub_cred_create(0, 0, 0, NULL), rump_pub_lwproc_curlwp());
	RZ(RUMP_VOP_LOOKUP(rumpns_rootvnode, &vp, cn));
	rump_pub_freecn(cn, RUMPCN_FREECRED);

	/* and then with modification-in-the-middle */
	cn = rump_pub_makecn(RUMP_NAMEI_LOOKUP, 0, pathstr, strlen(pathstr),
	    rump_pub_cred_create(0, 0, 0, NULL), rump_pub_lwproc_curlwp());
	strcpy(pathstr, "/muuta");
	RZ(RUMP_VOP_LOOKUP(rumpns_rootvnode, &vp, cn));
	rump_pub_freecn(cn, RUMPCN_FREECRED);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, makecn);

	return atf_no_error();
}
