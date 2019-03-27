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
# Copyright (c) 2011, Joyent Inc. All rights reserved.
# Use is subject to license terms.
#

#
# Test to catch that we properly look for libraries dependencies in
# our full library parth
#

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

libdira=${TMPDIR:-/tmp}/libdepa.$$
libdirb=${TMPDIR:-/tmp}/libdepb.$$
libdirc=${TMPDIR:-/tmp}/libdepc.$$
dtrace=$1

setup_libs()
{
        mkdir $libdira
        mkdir $libdirb
        mkdir $libdirc
        cat > $libdira/liba.$$.d <<EOF
#pragma D depends_on library libb.$$.d
#pragma D depends_on library libc.$$.d
#pragma D depends_on library libd.$$.d
EOF
        cat > $libdirb/libb.$$.d <<EOF
#pragma D depends_on library libc.$$.d
EOF
        cat > $libdirb/libc.$$.d <<EOF
EOF
        cat > $libdirb/libd.$$.d <<EOF
EOF
        cat > $libdirc/libe.$$.d <<EOF
#pragma D depends_on library liba.$$.d
EOF
        cat > $libdirc/libf.$$.d <<EOF
EOF
}


setup_libs

$dtrace -L$libdira -L$libdirb -L$libdirc -e

status=$?
rm -rf $libdira
rm -rf $libdirb
rm -rf $libdirc
return $status

