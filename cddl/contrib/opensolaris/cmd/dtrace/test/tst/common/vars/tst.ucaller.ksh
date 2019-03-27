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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# ident	"%Z%%M%	%I%	%E% SMI"

#
# This test is a bit naughty; it's assuming that ld.so.1 has an implementation
# of calloc(3C), and that it's implemented in terms of the ld.so.1
# implementation of malloc(3C).  If you're reading this comment because
# those assumptions have become false, please accept my apologies...
#
if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1

$dtrace -qs /dev/stdin -c "/bin/echo" <<EOF
pid\$target:ld.so.1:calloc:entry
{
	self->calloc = 1;
}

pid\$target:ld.so.1:malloc:entry
/self->calloc/
{
	@[umod(ucaller), ufunc(ucaller)] = count();
}

pid\$target:ld.so.1:calloc:return
/self->calloc/
{
	self->calloc = 0;
}

END
{
	printa("%A %A\n", @);
}
EOF

exit 0
