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
# This test verifies that probes will be picked up after a dlopen(3C)
# when the pid provider is specified as a glob (e.g., p*d$target.)
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
all: main altlib.so

main: main.o
	cc -o main main.o

main.o: main.c
	cc -c main.c

altlib.so: altlib.o
	cc -shared -o altlib.so altlib.o -lc

altlib.o: altlib.c
	cc -c altlib.c
EOF

cat > altlib.c <<EOF
void
go(void)
{
}
EOF

cat > main.c <<EOF
#include <dlfcn.h>
#include <unistd.h>
#include <stdio.h>

void
go(void)
{
}

int
main(int argc, char **argv)
{
	void *alt;
	void *alt_go;

	go();

	if ((alt = dlopen("./altlib.so", RTLD_LAZY | RTLD_LOCAL)) 
	    == NULL) {
		printf("dlopen of altlib.so failed: %s\n", dlerror());
		return (1);
	}

	if ((alt_go = dlsym(alt, "go")) == NULL) {
		printf("failed to lookup 'go' in altlib.so\n");
		return (1);
	}

	((void (*)(void))alt_go)();

	return (0);
}
EOF

make > /dev/null
if [ $? -ne 0 ]; then
	print -u2 "failed to build"
	exit 1
fi

cat > main.d <<'EOF'
p*d$target::go:entry
{
	@foo[probemod, probefunc, probename] = count();
}

END
{
	printa("%s:%s:%s %@u\n",@foo);
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
