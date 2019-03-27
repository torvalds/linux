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
# This script tests that the firing order of probes in a process is:
# 
#  1.  proc:::start
#  2.  proc:::lwp-start
#  3.  proc:::lwp-exit
#  4.  proc:::exit
#
# If this fails, the script will run indefinitely; it relies on the harness
# to time it out.
#
script()
{
	$dtrace -s /dev/stdin <<EOF
	proc:::start
	/curpsinfo->pr_ppid == $child/
	{
		self->start = 1;
	}

	proc:::lwp-start
	/self->start/
	{
		self->lwp_start = 1;
	}

	proc:::lwp-exit
	/self->lwp_start/
	{
		self->lwp_exit = 1;
	}

	proc:::exit
	/self->lwp_exit == 1/
	{
		exit(0);
	}
EOF
}

sleeper()
{
	while true; do
		/usr/bin/sleep 1
	done
}

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1

sleeper &
child=$!

script
status=$?

kill $child
exit $status
