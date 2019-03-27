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

dtrace=$1
DIR=/var/tmp/dtest.$$

mkdir $DIR
cd $DIR

cat > test.c <<EOF
#include <sys/sdt.h>

int
main(int argc, char **argv)
{
	DTRACE_PROBE(test_prov, zero);
	DTRACE_PROBE1(test_prov, one, 1);
	DTRACE_PROBE2(test_prov, two, 2, 3);
	DTRACE_PROBE3(test_prov, three, 4, 5, 7);
	DTRACE_PROBE4(test_prov, four, 7, 8, 9, 10);
	DTRACE_PROBE5(test_prov, five, 11, 12, 13, 14, 15);
}
EOF

cat > prov.d <<EOF
provider test_prov {
	probe zero();
	probe one(uintptr_t);
	probe two(uintptr_t, uintptr_t);
	probe three(uintptr_t, uintptr_t, uintptr_t);
	probe four(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
	probe five(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
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

cd /
/bin/rm -rf $DIR
