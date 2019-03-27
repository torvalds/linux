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

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
DIR=/var/tmp/dtest.$$

mkdir $DIR
cd $DIR

cat > test.c <<EOF
#include <sys/sdt.h>

int
main(int argc, char **argv)
{
	DTRACE_PROBE(test_prov, entry);
	DTRACE_PROBE(test_prov, __entry);
	DTRACE_PROBE(test_prov, foo__entry);
	DTRACE_PROBE(test_prov, carpentry);
	DTRACE_PROBE(test_prov, miniatureturn);
	DTRACE_PROBE(test_prov, foo__return);
	DTRACE_PROBE(test_prov, __return);
	/*
	 * Unfortunately, a "return" probe is not currently possible due to
	 * the conflict with a reserved word.
	 */
	DTRACE_PROBE(test_prov, done);
}
EOF

cat > prov.d <<EOF
provider test_prov {
	probe entry();
	probe __entry();
	probe foo__entry();
	probe carpentry();
	probe miniatureturn();
	probe foo__return();
	probe __return();
	probe done();
};
EOF

cc -c test.c
if [ $? -ne 0 ]; then
	print -u2 "failed to compile test.c"
	exit 1
fi
$dtrace -G -s prov.d test.o
if [ $? -ne 0 ]; then
	print -u2 "failed to create DOF"
	exit 1
fi
cc -o test test.o prov.o
if [ $? -ne 0 ]; then
	print -u2 "failed to link final executable"
	exit 1
fi

script()
{
	$dtrace -wqZFs /dev/stdin <<EOF
	BEGIN
	{
		system("$DIR/test");
		printf("\n");
	}

	test_prov*:::done
	/progenyof(\$pid)/
	{
		exit(0);
	}

	test_prov*:::
	/progenyof(\$pid)/
	{
		printf("\n");
	}
EOF
}

script | cut -c5-
status=$?

cd /
/bin/rm -rf $DIR

exit $status
