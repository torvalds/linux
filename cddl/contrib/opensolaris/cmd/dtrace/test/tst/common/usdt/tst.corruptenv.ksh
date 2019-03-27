#!/usr/bin/ksh
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# This test verifies that a program that corrupts its own environment
# without inducing a crash does not crash solely due to drti.o's use of
# getenv(3C).
#

PATH=/usr/bin:/usr/sbin:$PATH

if (( $# != 1 )); then
	print -u2 'expected one argument: <dtrace-path>'
	exit 2
fi

#
# jdtrace does not implement the -h option that is required to generate
# C header files.
#
if [[ "$1" == */jdtrace ]]; then
	exit 0
fi

dtrace="$1"
startdir="$PWD"
dir=$(mktemp -d -t drtiXXXXXX)
if (( $? != 0 )); then
	print -u2 'Could not create safe temporary directory'
	exit 2
fi

cd "$dir"

cat > Makefile <<EOF
all: main

main: main.o prov.o
	\$(CC) -o main main.o prov.o

main.o: main.c prov.h
	\$(CC) -c main.c

prov.h: prov.d
	$dtrace -h -s prov.d

prov.o: prov.d main.o
	$dtrace -G -s prov.d main.o
EOF

cat > prov.d <<EOF
provider tester {
	probe entry();
};
EOF

cat > main.c <<EOF
#include <stdlib.h>
#include <sys/sdt.h>
#include "prov.h"

int
main(int argc, char **argv, char **envp)
{
	envp[0] = (char*)0xff;
	TESTER_ENTRY();
	return 0;
}
EOF

make > /dev/null
status=$?
if (( $status != 0 )) ; then
	print -u2 "failed to build"
else
	./main
	status=$?
fi

cd "$startdir"
rm -rf "$dir"

exit $status
