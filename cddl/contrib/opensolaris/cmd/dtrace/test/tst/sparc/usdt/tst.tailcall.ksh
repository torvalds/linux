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

#
# ASSERTION: Make sure USDT probes work as tail-calls on SPARC.
#

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
DIR=/var/tmp/dtest.$$

mkdir $DIR
cd $DIR

cat > test.s <<EOF
#include <sys/asm_linkage.h>

	DGDEF(__fsr_init_value)
	.word 0

	ENTRY(test)
	save	%sp, -SA(MINFRAME + 4), %sp
	mov	9, %i0
	mov	19, %i1
	mov	2006, %i2
	call	__dtrace_test___fire
	restore
	SET_SIZE(test)

	ENTRY(main)
	save	%sp, -SA(MINFRAME + 4), %sp

1:
	call	test
	nop

	ba	1b
	nop

	ret
	restore	%g0, %g0, %o0
	SET_SIZE(main)
EOF

cat > prov.d <<EOF
provider test {
	probe fire(int, int, int);
};
EOF

/usr/bin/as -xregsym=no -P -D_ASM -o test.o test.s
if [ $? -ne 0 ]; then
	print -u2 "failed to compile test.s"
	exit 1
fi

$dtrace -G -32 -s prov.d test.o
if [ $? -ne 0 ]; then
	print -u2 "failed to create DOF"
	exit 1
fi

cc -o test test.o prov.o
if [ $? -ne 0 ]; then
	print -u2 "failed to link final executable"
	exit 1
fi

$dtrace -c ./test -s /dev/stdin <<EOF
test\$target:::fire
/arg0 == 9 && arg1 == 19 && arg2 == 2006/
{
	printf("%d/%d/%d", arg0, arg1, arg2);
	exit(0);
}

test\$target:::fire
{
	printf("%d/%d/%d", arg0, arg1, arg2);
	exit(1);
}

BEGIN
{
	/*
	 * Let's just do this for 5 seconds.
	 */
	timeout = timestamp + 5000000000;
}

profile:::tick-4
/timestamp > timeout/
{
	trace("test timed out");
	exit(1);
}
EOF

status=$?

cd /
/bin/rm -rf $DIR

exit $status
