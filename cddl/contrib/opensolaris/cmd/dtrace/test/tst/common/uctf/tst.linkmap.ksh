#! /usr/bin/ksh
#
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2013 Joyent, Inc. All rights reserved.
#

#
# We should be able to see both strstr from libc and from ld on an
# alternate linkmap.
#

if [ $# != 1 ]; then
        echo expected one argument: '<'dtrace-path'>'
        exit 2
fi

dtrace=$1

$dtrace -q -p $$ -s /dev/stdin  <<EOF
pid\$target:LM1\`ld.so.1:strstr:entry,
pid\$target:libc.so.1:strstr:entry
{
	exit (0);
}

BEGIN
{
	exit (0);
}
EOF
rc=$?

exit $rc
