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
# Copyright (c) 2013, Joyent, Inc. All rights reserved.
#

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
DIR=/var/tmp/dtest.$$

mkdir $DIR
cd $DIR

echo '#pragma D option quiet' > test.d
echo '#pragma D option aggsortkey' >> test.d

cat > test.c <<EOF
#include <unistd.h>

void
main()
{
EOF

objs=

for oogle in doogle bagnoogle; do
	cat > $oogle.c <<EOF
#include <sys/sdt.h>

void
$oogle()
{
	DTRACE_PROBE($oogle, knows);
}
EOF

	cat > $oogle.d <<EOF
provider $oogle {
	probe knows();
};
EOF

	cc -c $oogle.c

	if [ $? -ne 0 ]; then
		print -u2 "failed to compile $oogle.c"
		exit 1
	fi

	$dtrace -G -s $oogle.d $oogle.o -o $oogle.d.o

	if [ $? -ne 0 ]; then
		print -u2 "failed to process $oogle.d"
		exit 1
	fi

	objs="$objs $oogle.o $oogle.d.o"
	echo $oogle'();' >> test.c
	echo $oogle'$target:::{@[probefunc] = count()}' >> test.d
done

echo "}" >> test.c

echo 'END{printa("%-10s %@d\\n", @)}' >> test.d

cc -o test test.c $objs

if [ $? -ne 0 ]; then
	print -u2 "failed to compile test.c"
	exit 1
fi

$dtrace -s ./test.d -Zc ./test

if [ $? -ne 0 ]; then
	print -u2 "failed to execute test"
	exit 1
fi

cd /
rm -rf $DIR
exit 0
