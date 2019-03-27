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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1

#
# Let's see if we can successfully specify a module using partial
# matches as well as the full module name. We'll use 'libc.so.1'
# (and therefore 'libc' and 'libc.so') as it's definitely there.
#

for lib in libc libc.so libc.so.1 'lib[c]*'; do
	sleep 60 &
	pid=$!
	dtrace -n "pid$pid:$lib::entry" -n 'tick-2s{exit(0);}'
	status=$?

	kill $pid

	if [ $status -gt 0 ]; then
		exit $status
	fi
done

exit $status
