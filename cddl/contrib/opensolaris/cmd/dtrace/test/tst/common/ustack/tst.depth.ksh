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

$dtrace -o $file -c date -s /dev/stdin <<EOF

	#pragma D option quiet
	#pragma D option bufsize=1M
	#pragma D option bufpolicy=fill

	pid\$target:::entry,
	pid\$target:::return,
	pid\$target:a.out::,
	syscall:::return,
	profile:::profile-997
	/pid == \$target/
	{
        	printf("START %s:%s:%s:%s\n",
            	probeprov, probemod, probefunc, probename);
        	trace(ustackdepth);
        	ustack(100);
        	trace("END\n");
	}

	tick-1sec
	/n++ == 10/
	{
		trace("test timed out...");
		exit(1);
	}
EOF

status=$?
if [ "$status" -ne 0 ]; then
	echo $tst: dtrace failed
	exit $status
fi

perl /dev/stdin $file <<EOF
	while (<>) {
		chomp;

		last if /^\$/;

		die "expected START at \$.\n" unless /^START/;

		\$_ = <>;
		chomp;
		die "expected depth (\$_) at \$.\n" unless /^(\d+)\$/;
		\$depth = \$1;

		for (\$i = 0; \$i < \$depth; \$i++) {
			\$_ = <>;
			chomp;
			die "unexpected END at \$.\n" if /^END/;
		}

		\$_ = <>;
		chomp;
		die "expected END at \$.\n" unless /^END\$/;
	}
EOF

status=$?

count=`wc -l $file | cut -f1 -do`
if [ "$count" -lt 1000 ]; then
	echo $tst: output was too short
	status=1
fi


if [ "$status" -eq 0 ]; then
	rm -f $file
fi

exit $status
