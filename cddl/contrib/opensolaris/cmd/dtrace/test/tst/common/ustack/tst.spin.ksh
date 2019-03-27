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

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

file=out.$$
dtrace=$1

rm -f $file

dir=`dirname $tst`

$dtrace -o $file -c $dir/tst.spin.exe -s /dev/stdin <<EOF

	#pragma D option quiet
	#pragma D option destructive
	#pragma D option evaltime=main

	/*
	 * Toss out the first 100 samples to wait for the program to enter
	 * its steady state.
	 */

	profile-1999
	/pid == \$target && n++ > 100/
	{
		@total = count();
		@stacks[ustack(4)] = count();
	}

	tick-1s
	{
		secs++;
	}

	tick-1s
	/secs > 5/
	{
		done = 1;
	}

	tick-1s
	/secs > 10/
	{
		trace("test timed out");
		exit(1);
	}

	profile-1999
	/pid == \$target && done/
	{
		raise(SIGINT);
		exit(0);
	}

	END
	{
		printa("TOTAL %@u\n", @total);
		printa("START%kEND\n", @stacks);
	}
EOF

status=$?
if [ "$status" -ne 0 ]; then
	echo $tst: dtrace failed
	exit $status
fi

perl /dev/stdin $file <<EOF
	\$_ = <>;
	chomp;
	die "output problem\n" unless /^TOTAL (\d+)/;
	\$count = \$1;
	die "too few samples (\$count)\n" unless \$count >= 1000;

	while (<>) {
		chomp;

		last if /^$/;

		die "expected START at \$.\n" unless /^START/;


		\$_ = <>;
		chomp;
		die "expected END at \$.\n" unless /\`baz\+/;

		\$_ = <>;
		chomp;
		die "expected END at \$.\n" unless /\`bar\+/;

		\$_ = <>;
		chomp;
		die "expected END at \$.\n" unless /\`foo\+/;

		\$_ = <>;
		chomp;
		die "expected END at \$.\n" unless /\`main\+/;

		\$_ = <>;
		chomp;
		die "expected END at \$.\n" unless /^END\$/;
	}

EOF

status=$?
if [ "$status" -eq 0 ]; then
	rm -f $file
fi

exit $status
