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

#
# This script attempts to tease out a race when probes are initially enabled.
#
script()
{
	#
	# Nauseatingly, the #defines below must be in the 0th column to
	# satisfy the ancient cpp that -C defaults to.
	#
	$dtrace -C -s /dev/stdin <<EOF
#define	PROF1		profile:::profile-4000hz
#define	PROF4		PROF1, PROF1, PROF1, PROF1
#define	PROF16		PROF4, PROF4, PROF4, PROF4
#define	PROF64		PROF16, PROF16, PROF16, PROF16
#define	PROF256		PROF64, PROF64, PROF64, PROF64
#define	PROF512		PROF256, PROF256

	PROF1
	{
		this->x = 0;
	}

	PROF512
	{
		this->x++;
	}

	PROF1
	/this->x != 512/
	{
		printf("failed! x is %d (expected 512)", this->x);
		exit(1);
	}

	tick-1sec
	/secs++/
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
let i=0

while [ "$i" -lt 20 ]; do
	script
	status=$?

	if [ "$status" -ne 0 ]; then
		exit $status
	fi

	let i=i+1
done

exit 0
