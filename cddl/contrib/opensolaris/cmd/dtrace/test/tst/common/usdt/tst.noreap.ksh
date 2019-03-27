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
# Copyright (c) 2011, Joyent, Inc. All rights reserved.
#

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
DIR=/var/tmp/dtest.$$

mkdir $DIR
cd $DIR

cat > test.c <<EOF
#include <unistd.h>
#include <sys/sdt.h>

int
main(int argc, char **argv)
{
	DTRACE_PROBE(test_prov, probe1);
}
EOF

cat > prov.d <<EOF
provider test_prov {
	probe probe1();
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
	$dtrace -Zwqs /dev/stdin <<EOF

	BEGIN
	{
		spec = speculation();
		speculate(spec);
		printf("this is speculative!\n");
	}

	test_prov*:::
	{
		probeid = id;
	}

	tick-1sec
	/probeid == 0/
	{
		printf("launching test\n");
		system("./test");
	}

	tick-1sec
	/probeid != 0/
	{
		printf("attempting re-enabling\n");
		system("dtrace -e -x errtags -i %d", probeid);
		attempts++;
	}

	tick-1sec
	/attempts > 10/
	{
		exit(0);
	}
EOF
}

script 2>&1 | tee test.out

#
# It should be true that our probe was not reaped after the provider was made
# defunct: the speculative tracing action prevents reaping of any ECB in the
# enabling.
# 
status=0

if grep D_PDESC_INVAL test.out 2> /dev/null 1>&2 ; then
	status=1
else
	grep D_PROC_GRAB test.out 2> /dev/null 1>&2
	status=$?
fi

cd /
rm -rf $DIR

exit $status
