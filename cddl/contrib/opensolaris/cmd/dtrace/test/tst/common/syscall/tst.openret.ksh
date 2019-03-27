#!/bin/ksh -p
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

script() {
	$dtrace -c 'cat shajirosan' -qs /dev/stdin <<EOF
	syscall::open*:entry
	/pid == \$target/
	{
		self->p = arg0;
	}

	syscall::open*:return
	/self->p && copyinstr(self->p) == "shajirosan"/
	{
		self->err = 1;
		self->p = 0;
	}

	syscall::open*:return
	/self->err && (int)arg0 == -1 && (int)arg1 == -1/
	{
		exit(0);
	}

	syscall::open*:return
	/self->err/
	{
		printf("a failed open(2) returned %d\n", (int)arg0);
		exit(1);
	}

	syscall::open*:return
	/self->p/
	{
		self->p = 0;
	}
EOF
}

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1

script
status=$?

exit $status
