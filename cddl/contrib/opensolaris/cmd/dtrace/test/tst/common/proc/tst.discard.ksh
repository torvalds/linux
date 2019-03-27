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
# This script tests that the proc:::signal-discard probe fires correctly
# and with the correct arguments.
#
# If this fails, the script will run indefinitely; it relies on the harness
# to time it out.
#
script()
{
	$dtrace -s /dev/stdin <<EOF
	proc:::signal-discard
	/args[1]->p_pid == $child &&
	    xlate<psinfo_t *>(args[1])->pr_psargs == "$longsleep" &&
	    args[2] == SIGHUP/
	{
		exit(0);
	}
EOF
}

killer()
{
	while true; do
		sleep 1
		kill -HUP $child
	done
}

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
longsleep="/bin/sleep 10000"

/usr/bin/nohup $longsleep &
child=$!

killer &
killer=$!
script
status=$?

kill $child
kill $killer

exit $status
