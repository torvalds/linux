#!/bin/ksh -p
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
# ident	"%Z%%M%	%I%	%E% SMI"

#
# This test verifies that a regex in the provider name will match
# USDT probes as well as pid probes (e.g., p*d$target matches both 
# pid$target and pyramid$target.)
#

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

dtrace=$1
DIR=${TMPDIR:-/tmp}/dtest.$$

mkdir $DIR
cd $DIR

cat > Makefile <<EOF
all: main

main: main.o prov.o
	cc -o main main.o prov.o

main.o: main.c prov.h
	cc -c main.c

prov.h: prov.d
	$dtrace -h -s prov.d

prov.o: prov.d main.o
	$dtrace -G -s prov.d main.o
EOF

cat > prov.d <<EOF
provider pyramid {
	probe entry();
};
EOF

cat > main.c <<EOF
#include <sys/sdt.h>
#include "prov.h"

int
main(int argc, char **argv)
{
	PYRAMID_ENTRY();
}
EOF

make > /dev/null
if [ $? -ne 0 ]; then
	print -u2 "failed to build"
	exit 1
fi

cat > main.d <<'EOF'
p*d$target::main:entry
{
	printf("%s:%s:%s\n", probemod, probefunc, probename);
}
EOF

script() {
	$dtrace -q -s ./main.d -c ./main
}

script
status=$?

cd /tmp
/bin/rm -rf $DIR

exit $status
