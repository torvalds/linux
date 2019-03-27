#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2012, Joyent, Inc. All rights reserved.
#

ppriv -s A=basic,dtrace_proc,dtrace_user $$

#
# When we have dtrace_proc (but lack dtrace_kernel), we expect to be able to
# read certain curpsinfo/curlwpsinfo/curcpu fields even though they require
# reading in-kernel state.  However, there are other fields in these translated
# structures that we know we shouldn't be able to read, as they require reading
# in-kernel state that we cannot read with only dtrace_proc.  Finally, there
# are a few fields that we may or may not be able to read depending on the
# specifics of context.  This test therefore asserts that we can read what we
# think we should be able to, that we can't read what we think we shouldn't be
# able to, and (for purposes of completeness) that we are indifferent about
# what we cannot assert one way or the other.
#
/usr/sbin/dtrace -q -Cs /dev/stdin <<EOF

#define CANREAD(what, field) \
    BEGIN { errmsg = "can't read field from what"; printf("field: "); \
	trace(what->field); printf("\n"); }

#define CANTREAD(what, field) \
    BEGIN { errmsg = ""; trace(what->field); \
	printf("\nable to successfully read field from what!"); exit(1); }

#define MIGHTREAD(what, field) \
    BEGIN { errmsg = ""; printf("field: "); trace(what->field); printf("\n"); }

#define CANREADVAR(vname) \
    BEGIN { errmsg = "can't read vname"; printf("vname: "); \
	trace(vname); printf("\n"); }

#define CANTREADVAR(vname) \
    BEGIN { errmsg = ""; trace(vname); \
	printf("\nable to successfully read vname!"); exit(1); }

#define MIGHTREADVAR(vname) \
    BEGIN { errmsg = ""; printf("vname: "); trace(vname); printf("\n"); }

CANREAD(curpsinfo, pr_pid)
CANREAD(curpsinfo, pr_nlwp)
CANREAD(curpsinfo, pr_ppid)
CANREAD(curpsinfo, pr_uid)
CANREAD(curpsinfo, pr_euid)
CANREAD(curpsinfo, pr_gid)
CANREAD(curpsinfo, pr_egid)
CANREAD(curpsinfo, pr_addr)
CANREAD(curpsinfo, pr_start)
CANREAD(curpsinfo, pr_fname)
CANREAD(curpsinfo, pr_psargs)
CANREAD(curpsinfo, pr_argc)
CANREAD(curpsinfo, pr_argv)
CANREAD(curpsinfo, pr_envp)
CANREAD(curpsinfo, pr_dmodel)

/*
 * If our p_pgidp points to the same pid structure as our p_pidp, we will
 * be able to read pr_pgid -- but we won't if not.
 */
MIGHTREAD(curpsinfo, pr_pgid)

CANTREAD(curpsinfo, pr_sid)
CANTREAD(curpsinfo, pr_ttydev)
CANTREAD(curpsinfo, pr_projid)
CANTREAD(curpsinfo, pr_zoneid)
CANTREAD(curpsinfo, pr_contract)

CANREAD(curlwpsinfo, pr_flag)
CANREAD(curlwpsinfo, pr_lwpid)
CANREAD(curlwpsinfo, pr_addr)
CANREAD(curlwpsinfo, pr_wchan)
CANREAD(curlwpsinfo, pr_stype)
CANREAD(curlwpsinfo, pr_state)
CANREAD(curlwpsinfo, pr_sname)
CANREAD(curlwpsinfo, pr_syscall)
CANREAD(curlwpsinfo, pr_pri)
CANREAD(curlwpsinfo, pr_onpro)
CANREAD(curlwpsinfo, pr_bindpro)
CANREAD(curlwpsinfo, pr_bindpset)

CANTREAD(curlwpsinfo, pr_clname)
CANTREAD(curlwpsinfo, pr_lgrp)

CANREAD(curcpu, cpu_id)

CANTREAD(curcpu, cpu_pset)
CANTREAD(curcpu, cpu_chip)
CANTREAD(curcpu, cpu_lgrp)
CANTREAD(curcpu, cpu_info)

/*
 * We cannot assert one thing or another about the variable "root":  for those
 * with only dtrace_proc, it will be readable in the global but not readable in
 * the non-global.
 */
MIGHTREADVAR(root)

CANREADVAR(cpu)
CANTREADVAR(pset)
CANTREADVAR(cwd)
CANTREADVAR(chip)
CANTREADVAR(lgrp)

BEGIN
{
	exit(0);
}

ERROR
/errmsg != ""/
{
	printf("fatal error: %s", errmsg);
	exit(1);
}
