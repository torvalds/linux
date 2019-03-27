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
# This test verifies that we only use the first entry of a file with a given
# name in the library path
#

if [ $# != 1 ]; then
	echo expected one argument: '<'dtrace-path'>'
	exit 2
fi

firstinc=${TMPDIR:-/tmp}/firstinc.$$
secondinc=${TMPDIR:-/tmp}/secondinc.$$
expexit=23

setup_include()
{
	mkdir $firstinc
	mkdir $secondinc
	cat > $firstinc/lib.d <<EOF
inline int foobar = $expexit;
#pragma D binding "1.0" foobar
EOF
	cat > $secondinc/lib.d <<EOF
inline int foobar = 42;
#pragma D binding "1.0" foobar
EOF
}

clean()
{
	rm -rf $firstinc
	rm -rf $secondinc
}

fail()
{
	echo "$@"
	clean
	exit 1
}

setup_include

dtrace -L$firstinc -L$secondinc -e -n 'BEGIN{ exit(foobar) }'
[[ $? != 0 ]] && fail "Failed to compile with same file in include path twice"
dtrace -L$firstinc -L$secondinc -n 'BEGIN{ exit(foobar) }'
status=$?
[[ $status != $expexit ]] && fail "Exited with unexpected status code: $status"
clean
exit 0
