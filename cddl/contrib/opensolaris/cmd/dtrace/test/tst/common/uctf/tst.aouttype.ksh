#! /usr/bin/ksh
#
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
# Copyright (c) 2013 Joyent, Inc. All rights reserved.
#

#
# Lookup a type that is inside a.out.
#

if [ $# != 1 ]; then
        echo expected one argument: '<'dtrace-path'>'
        exit 2
fi

dtrace=$1
t="season_7_lisa_the_vegetarian_t *"
exe="tst.aouttype.exe"

elfdump -c "./$exe" | grep -Fq 'sh_name: .SUNW_ctf' 
if [[ $? -ne 0 ]]; then
	echo "CTF does not exist in $exe, that's a bug" >&2
	exit 1
fi

./$exe &
pid=$!

$dtrace -n "BEGIN{ trace((pid$pid\`$t)0); exit(0); }"
rc=$?

kill -9 $pid

exit $rc
