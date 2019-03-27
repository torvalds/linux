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
# This test is checking that we can read members and that pointers inside
# members point to valid data that is intelligible, eg. strings.
#

if [ $# != 1 ]; then
        echo expected one argument: '<'dtrace-path'>'
        exit 2
fi

dtrace=$1
exe="tst.chasestrings.exe"

elfdump -c "./$exe" | grep -Fq 'sh_name: .SUNW_ctf' 
if [[ $? -ne 0 ]]; then
	echo "CTF does not exist in $exe, that's a bug" >&2
	exit 1
fi

./$exe &
pid=$!

$dtrace -qs /dev/stdin <<EOF
pid$pid::has_princess:entry
/next == 0/
{
	printf("game: %s, dungeon: %d, villain: %s, zelda: %d\n",
	    stringof(args[0]->zi_gamename), args[0]->zi_ndungeons,
	    stringof(args[0]->zi_villain), args[0]->zi_haszelda);
	next = 1;
}

pid$pid::has_dungeons:entry
/next == 1/
{
	printf("game: %s, dungeon: %d, villain: %s, zelda: %d\n",
	    stringof(args[0]->zi_gamename), args[0]->zi_ndungeons,
	    stringof(args[0]->zi_villain), args[0]->zi_haszelda);
	next = 2;
}

pid$pid::has_villain:entry
/next == 2/
{
	printf("game: %s, dungeon: %d, villain: %s, zelda: %d\n",
	    stringof(args[0]->zi_gamename), args[0]->zi_ndungeons,
	    stringof(args[0]->zi_villain), args[0]->zi_haszelda);
	exit(0);
}
EOF
rc=$?

kill -9 $pid

exit $rc
