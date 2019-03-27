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

/usr/sbin/dtrace -q -Cs /dev/stdin <<EOF

#define CANREAD(field) \
	BEGIN { this->fp = getf(0); errmsg = "can't read field"; \
	    printf("field: "); trace(this->fp->field); printf("\n"); }

#define CANTREAD(field) \
	BEGIN { errmsg = ""; this->fp = getf(0); trace(this->fp->field); \
	    printf("\nable to successfully read field!"); exit(1); }

CANREAD(f_flag)
CANREAD(f_flag2)
CANREAD(f_vnode)
CANREAD(f_offset)
CANREAD(f_cred)
CANREAD(f_audit_data)
CANREAD(f_count)

/*
 * We can potentially read parts of our cred, but we can't dereference
 * through cr_zone.
 */
CANTREAD(f_cred->cr_zone->zone_id)

CANREAD(f_vnode->v_path)
CANREAD(f_vnode->v_op)
CANREAD(f_vnode->v_op->vnop_name)

CANTREAD(f_vnode->v_flag)
CANTREAD(f_vnode->v_count)
CANTREAD(f_vnode->v_pages)
CANTREAD(f_vnode->v_type)
CANTREAD(f_vnode->v_vfsmountedhere)
CANTREAD(f_vnode->v_op->vop_open)

BEGIN
{
	errmsg = "";
	this->fp = getf(0);
	this->fp2 = getf(1);

	trace(this->fp->f_vnode);
	printf("\nable to successfully read this->fp!");
	exit(1);
}

BEGIN
{
	errmsg = "";
	this->fp = getf(0);
}

BEGIN
{
	trace(this->fp->f_vnode);
	printf("\nable to successfully read this->fp from prior clause!");
}

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
	
EOF
