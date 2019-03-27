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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#


#
# This tests that enablings from the cpc provider will fail if cpustat(1) is
# already master of the universe.
#
# This script will fail if:
#       1) The system under test does not define the 'PAPI_tot_ins'
#       generic event.

script()
{
        $dtrace -s /dev/stdin <<EOF
        #pragma D option bufsize=128k

        BEGIN
        {
                exit(0);
        }

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

cpustat -c PAPI_tot_ins 1 20 &
pid=$!
sleep 5
script 2>/dev/null

status=$?

kill $pid
exit $status
