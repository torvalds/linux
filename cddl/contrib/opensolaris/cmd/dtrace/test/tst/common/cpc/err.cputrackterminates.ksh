#!/bin/ksh
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.

#
# This script ensures that cputrack(1) will terminate when the cpc provider
# kicks into life.
#
# The script will fail if:
#	1) The system under test does not define the 'PAPI_tot_ins' event.
#

script()
{
	$dtrace -s /dev/stdin <<EOF
	#pragma D option bufsize=128k

	cpc:::PAPI_tot_ins-all-10000
	{
		@[probename] = count();
	}

	tick-1s
	/n++ > 10/
	{
		exit(0);
	}
EOF
}

if [ $# != 1 ]; then
        echo expected one argument: '<'dtrace-path'>'
        exit 2
fi

dtrace=$1

cputrack -c PAPI_tot_ins sleep 20 &
cputrack_pid=$!
sleep 5
script 2>/dev/null &

wait $cputrack_pid
status=$?

rm $dtraceout

exit $status
