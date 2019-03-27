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
# This script tests that the proc:::exit probe fires with the correct argument
# when the process core dumps.  The problematic bit here is making sure that
# a process _can_ dump core -- if core dumps are disabled on both a global
# and per-process basis, this test will fail.  Rather than having this test
# muck with coreadm(1M) settings, it will fail explicitly in this case and
# provide a hint as to the problem.  In general, machines should never be
# running with both per-process and global core dumps disabled -- so this
# should be a non-issue in practice.
#
# If this fails, the script will run indefinitely; it relies on the harness
# to time it out.
#
script()
{
	$dtrace -s /dev/stdin <<EOF
	proc:::exit
	/curpsinfo->pr_ppid == $child &&
	    execargs == "$longsleep" && args[0] == CLD_DUMPED/
	{
		exit(0);
	}

	proc:::exit
	/curpsinfo->pr_ppid == $child &&
	    execargs == "$longsleep" && args[0] != CLD_DUMPED/
	{
		printf("Child process could not dump core.");
		exit(1);
	}
EOF
}

sleeper()
{
	while true; do
		$longsleep &
                /bin/sleep 1
		kill -SEGV $!
	done
}

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
longsleep="./tst.exitcore.exe"

sleeper &
child=$!

script
status=$?

kill $child

exit $status
