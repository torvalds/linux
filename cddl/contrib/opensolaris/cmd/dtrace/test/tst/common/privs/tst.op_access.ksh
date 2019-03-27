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
# Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#ident	"%Z%%M%	%I%	%E% SMI"

ppriv -s A=basic,dtrace_proc,dtrace_user $$

/usr/sbin/dtrace -q -s /dev/stdin <<"EOF"
BEGIN {
	errorcount = 0;
	expected_errorcount = 7;
}

/* BYREF */
BEGIN { trace(`utsname); }
BEGIN { trace(`kmem_flags); }

/* DIF_OP_SCMP */
BEGIN /`initname == "/sbin/init"/ { trace("bad"); }

/* DIF_OP_COPYS */
BEGIN { p = `p0; trace(p); }

/* DIF_OP_STTS */
BEGIN { self->p = `p0; trace(self->p); }

/* DIF_OP_STGAA */
BEGIN { a[stringof(`initname)] = 42; trace(a["/sbin/init"]); }

/* DIF_OP_STTAA */
BEGIN { self->a[stringof(`initname)] = 42; trace(self->a["/sbin/init"]); }

ERROR {
	errorcount++;
}

BEGIN /errorcount == expected_errorcount/ {
	trace("pass");
	exit(0);
}

BEGIN /errorcount != expected_errorcount/ {
	printf("fail: expected %d.  saw %d.", expected_errorcount, errorcount);
	exit(1);
}
EOF

exit $?
