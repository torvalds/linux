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
	$dtrace -wq -o $tmpfile -s /dev/stdin 2> $errfile <<EOF
	BEGIN
	{
		/*
		 * All of these should fail...
		 */
		freopen("..");
		freopen("%s", ".");
		freopen("%c%c", '.', '.');
		freopen("%c", '.');

		/*
		 * ...so stdout should still be open here.
		 */
		printf("%d", ++i);

		freopen("%s%s", ".", ".");
		freopen("%s%s", ".", ".");

		printf("%d", ++i);
	}

	BEGIN
	/i == 2/
	{
		/*
		 * ...and here.
		 */
		printf("%d\n", ++i);
		exit(0);
	}

	BEGIN
	{
		exit(1);
	}
EOF
}

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
tmpfile=/tmp/tst.badfreopen.$$
errfile=/tmp/tst.badfreopen.$$.stderr

script
status=$?

if [ "$status" -eq 0 ]; then
	i=`cat $tmpfile`

	if [[ $i != "123" ]]; then
		echo "$0: unexpected contents in $tmpfile: " \
		    "expected 123, found $i"
		status=100
	fi
	
	i=`wc -l $errfile | nawk '{ print $1 }'`

	if [ "$i" -lt 6 ]; then
		echo "$0: expected at least 6 lines of stderr, found $i lines"
		status=101
	fi
else
	cat $errfile > /dev/fd/2
fi

rm $tmpfile $errfile

exit $status
