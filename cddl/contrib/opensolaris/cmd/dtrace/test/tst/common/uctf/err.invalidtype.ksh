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
# While it's hard to be completely certain that a type of the name we want
# doesn't exist, we're going to try to pick a name which is rather unique.
#

if [ $# != 1 ]; then
        echo expected one argument: '<'dtrace-path'>'
        exit 2
fi

dtrace=$1
t="season_8_mountain_of_madness_t"
pid=$$

$dtrace -n "BEGIN{ trace(pid$pid\`$t)0); }"
rc=$?

exit $rc
