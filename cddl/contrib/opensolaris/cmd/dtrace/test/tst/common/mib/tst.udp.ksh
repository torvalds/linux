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
# This script tests that several of the the mib:::udp* probes fire and fire
# with a valid args[0].
#
script()
{
	$dtrace -s /dev/stdin <<EOF
	mib:::udpHCOutDatagrams
	{
		out = args[0];
	}

	mib:::udpHCInDatagrams
	{
		in = args[0];
	}

	profile:::tick-10msec
	/in && out/
	{
		exit(0);
	}
EOF
}

rupper()
{
	while true; do
		rup localhost
		/usr/bin/sleep 1
	done
}

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1

rupper &
rupper=$!
script
status=$?

kill $rupper
exit $status
