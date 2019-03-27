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
# ident	"%Z%%M%	%I%	%E% SMI"
#

#
# This script primarily tests that the ::dtrace dcmd is not dumping
# core. We don't try to make sense of the output of the dcmd nor
# do we check to see if any output is produced. We merely see if
# mdb fails with a fatal failure.
#

script()
{
	$dtrace -o $dtraceout -s /dev/stdin <<EOF
	syscall:::entry
	{
		@[probefunc] = count();
	}
EOF
}

mdbdoit()
{
	mdb -k <<EOF
	::walk dtrace_state | ::dtrace
EOF
	status=$?
	kill $script
}

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
dtraceout=/tmp/dtrace.out.$$
script &
script=$!
timeout=15

#
# Sleep while the above script fires into life. To guard against dtrace dying
# and us sleeping forever we allow 15 secs for this to happen. This should be
# enough for even the slowest systems.
#
while [ ! -f $dtraceout ]; do
	sleep 1
	timeout=$(($timeout-1))
	if [ $timeout -eq 0 ]; then
		echo "dtrace failed to start. Exiting."
		exit 1
	fi
done

mdbdoit

rm $dtraceout

exit $status
