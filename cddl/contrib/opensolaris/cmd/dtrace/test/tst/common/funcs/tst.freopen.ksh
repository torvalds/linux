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

script()
{
	$dtrace -wq -o $tmpfile -s /dev/stdin $tmpfile <<EOF
	BEGIN
	{
		i = 0;
	}

	tick-10ms
	{
		freopen("%s.%d", \$\$1, i);
		printf("%d\n", i)
	}

	tick-10ms
	/++i == $iter/
	{
		freopen("");
		printf("%d\n", i);
		exit(0);
	}
EOF
}

cleanup()
{
	let i=0

	if [ -f $tmpfile ]; then
		rm $tmpfile
	fi

	while [ "$i" -lt "$iter" ]; do
		if [ -f $tmpfile.$i ]; then
			rm $tmpfile.$i
		fi
		let i=i+1
	done
}

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
tmpfile=/tmp/tst.freopen.$$
iter=20

script
status=$?

let i=0

if [ -f $tmpfile.$iter ]; then
	echo "$0: did not expect to find file: $tmpfile.$iter"
	cleanup
	exit 100
fi

mv $tmpfile $tmpfile.$iter
let iter=iter+1

while [ "$i" -lt "$iter" ]; do
	if [ ! -f $tmpfile.$i ]; then
		echo "$0: did not find expected file: $tmpfile.$i"
		cleanup
		exit 101
	fi

	j=`cat $tmpfile.$i`

	if [ "$i" -ne "$j" ]; then
		echo "$0: unexpected contents in $tmpfile.$i: " \
		    "expected $i, found $j"
		cleanup
		exit 102
	fi

	rm $tmpfile.$i
	let i=i+1
done

exit $status
