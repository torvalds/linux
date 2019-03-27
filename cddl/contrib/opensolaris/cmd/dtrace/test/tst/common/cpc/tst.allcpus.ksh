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

#
# This script verifies that we can fire a probe on each CPU that is in
# an online state.
#
# The script will fail if:
#       1) The system under test does not define the 'PAPI_tot_ins' event.
#

if [ $# != 1 ]; then
        echo expected one argument: '<'dtrace-path'>'
        exit 2
fi

dtrace=$1
numproc=`psrinfo | tail -1 | cut -f1`
cpu=0
dtraceout=/var/tmp/dtrace.out.$$
scriptout=/var/tmp/script.out.$$

spin()
{
	while [ 1 ]; do
		:
	done
}

script()
{
        $dtrace -o $dtraceout -s /dev/stdin <<EOF
	#pragma D option bufsize=128k
	#pragma D option quiet

        cpc:::PAPI_tot_ins-user-10000
	/cpus[cpu] != 1/
        {
		cpus[cpu] = 1;
		@a[cpu] = count();
        }

	tick-1s
	/n++ > 10/
	{
		printa(@a);
		exit(0);
	}
EOF
}

echo "" > $scriptout
while [ $cpu -le $numproc ]
do
	if [ "`psrinfo -s $cpu 2> /dev/null`" -eq 1 ]; then
		printf "%9d %16d\n" $cpu 1 >> $scriptout
		spin &
		allpids[$cpu]=$!
		pbind -b $cpu $!
	fi
	cpu=$(($cpu+1))
done
echo "" >> $scriptout

script

diff $dtraceout $scriptout >/dev/null 2>&1
status=$?

# kill off the spinner processes
cpu=0
while [ $cpu -le $numproc ]
do
	if [ "`psrinfo -s $cpu 2> /dev/null`" -eq 1 ]; then
		kill ${allpids[$cpu]}
	fi
	cpu=$(($cpu+1))
done

rm $dtraceout
rm $scriptout

exit $status
