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
# This script ensures that cputrack(1M) will fail to start when the cpc
# provider has active enablings.
#
# The script will fail if:
#	1) The system under test does not define the 'PAPI_tot_ins' event.
#

script()
{
        $dtrace -o $dtraceout -s /dev/stdin <<EOF
        #pragma D option bufsize=128k

        cpc:::PAPI_tot_ins-all-10000
        {
                @[probename] = count();
        }
EOF
}


if [ $# != 1 ]; then
        echo expected one argument: '<'dtrace-path'>'
        exit 2
fi

dtrace=$1
dtraceout=/tmp/dtrace.out.$$
script 2>/dev/null &
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

cputrack -c PAPI_tot_ins sleep 10
status=$?

rm $dtraceout

exit $status
