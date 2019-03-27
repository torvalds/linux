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
#	temporal option causes output to be sorted
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

$dtrace -o $file -c 'sleep 3' -s /dev/stdin <<EOF
	#pragma D option quiet
	#pragma D option temporal

	BEGIN
	{
		@lines = count();
		printf("0 begin\n");
	}

	END
	{
		/* Bump @lines every time we print a line. */
		@lines = count();
		printf("%u end\n", timestamp);
		@lines = count();
		printa("99999999999999999 lines %@u\n", @lines);
	}

	profile-97hz
	{
		@lines = count();
		printf("%u\n", timestamp);
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

head -n 1 $file.2 | grep begin >/dev/null
status=$?
if [ "$status" -ne 0 ]; then
	echo $tst: begin probe did not fire
	exit $status
fi

tail -n 2 $file.2 | grep end >/dev/null
status=$?
if [ "$status" -ne 0 ]; then
	echo $tst: end probe did not fire
	exit $status
fi

if [ $(tail -n 1 $file.2 | cut -f3 -d ' ') -ne \
    $(wc -l $file.2) ]; then
	echo $tst: incorrect number of lines output
	exit 1
fi

exit $status
