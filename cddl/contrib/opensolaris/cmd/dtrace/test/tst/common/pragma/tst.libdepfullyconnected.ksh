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
# This test verifies that we can generate the correct ordering for
# a given dependency specification. All files either have a dependency
# on another file or are the dependent of another file. In this way we
# guarantee consistent ordering as no nodes in the dependency graph will
# be isolated.
#

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

tmpfile=/tmp/libdeporder.$$
libdir=${TMPDIR:-/tmp}/libdep.$$
dtrace=$1

setup_libs()
{
	mkdir $libdir
	cat > $libdir/liba.$$.d <<EOF
#pragma D depends_on library libd.$$.d
EOF
	cat > $libdir/libb.$$.d <<EOF
EOF
	cat > $libdir/libc.$$.d <<EOF
#pragma D depends_on library libe.$$.d
EOF
	cat > $libdir/libd.$$.d <<EOF
#pragma D depends_on library libb.$$.d
EOF
	cat > $libdir/libe.$$.d <<EOF
#pragma D depends_on library liba.$$.d
EOF
}


setup_libs

DTRACE_DEBUG=1 $dtrace -L$libdir -e >$tmpfile 2>&1 

perl /dev/stdin $tmpfile <<EOF

	@order = qw(libc libe liba libd libb);

	while (<>) {
		if (\$_ =~ /lib[a-e]\.[0-9]+.d/) {
			\$pos = length \$_;
			for (\$i=0; \$i<=1;\$i++) {
				\$pos = rindex(\$_, "/", \$pos);
				\$pos--;
			}

			push(@new, substr(\$_, \$pos+2, 4));
			next;
		}
		next;
	}

	exit 1 if @new != @order;

	while (@new) {
		exit 1 if pop(@new) ne pop(@order);
	}

	exit 0;
EOF


status=$?
rm -rf $libdir
rm $tmpfile
return $status
