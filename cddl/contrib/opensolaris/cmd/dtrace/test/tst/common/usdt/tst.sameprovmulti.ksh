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

#
# Copyright (c) 2015, Joyent, Inc. All rights reserved.
#

#
# This test assures that we can have the same provider name across multiple
# probe definitions, and that the result will be the union of those
# definitions.  In particular, libusdt depends on this when (for example)
# node modules that create a provider are loaded multiple times due to
# being included by different modules.
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

void
main()
{
EOF

objs=

for oogle in bagnoogle stalloogle cockoogle; do
	cat > $oogle.c <<EOF
#include <sys/sdt.h>

void
$oogle()
{
	DTRACE_PROBE(doogle, $oogle);
}
EOF

	cat > $oogle.d <<EOF
provider doogle {
	probe $oogle();
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
done

echo "}" >> test.c

cc -o test test.c $objs

if [ $? -ne 0 ]; then
	print -u2 "failed to compile test.c"
	exit 1
fi

$dtrace -n 'doogle$target:::{@[probename] = count()}' \
    -n 'END{printa("%-10s %@d\n", @)}' -x quiet -x aggsortkey -Zc ./test

if [ $? -ne 0 ]; then
	print -u2 "failed to execute test"
	exit 1
fi

cd /
rm -rf $DIR
exit 0
