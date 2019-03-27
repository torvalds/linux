#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2012 by Delphix. All rights reserved.
#

############################################################################
# ASSERTION:
#	temporal option causes output to be sorted, even when some
#	buffers are empty
#
# SECTION: Pragma
#
# NOTES: The temporal option has no effect on a single-CPU system, so
#    this needs to be run on a multi-CPU system to effectively test the
#    temporal option.
#
############################################################################

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
file=/tmp/out.$$

rm -f $file

$dtrace -o $file -s /dev/stdin <<EOF
	#pragma D option quiet
	#pragma D option destructive
	#pragma D option temporal
	#pragma D option switchrate=1000hz

	/*
	 * Use two enablings of the same probe, so that cpu 0 will always
	 * record its data just a little bit before the other cpus.
	 * We don't want to use the chill() action in the same enabling
	 * that we record the timestamp, because chill() causes the
	 * timestamp to be re-read, and thus not match the timestamp
	 * which libdtrace uses to sort the records.
	 */

	profile-401
	/cpu == 0/
	{
		printf("%d\n", timestamp);
	}

	profile-401
	/cpu != 0/
	{
		chill(1000); /* one microsecond */
	}

	profile-401
	/cpu != 0/
	{
		printf("%d\n", timestamp);
	}

	tick-1s
	/k++ == 10/
	{
		printf("%d\n", timestamp);
		exit(0);
	}
EOF

status=$?
if [ "$status" -ne 0 ]; then
	echo $tst: dtrace failed
	exit $status
fi

# dtrace outputs a blank line at the end, which will sort to the beginning,
# so use sed to remove the blank line.
sed '$d' $file > $file.2

sort -n $file.2 | diff $file.2 -
status=$?
if [ "$status" -ne 0 ]; then
	echo $tst: output is not sorted
	exit $status
fi

exit $status
