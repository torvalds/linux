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
# This test is purposefully using a 64-bit DTrace and thus 64-bit types
# when compared with a 32-bit process. This test uses the userland
# keyword and so the implicit copyin should access illegal memory and
# thus exit.
#

if [ $# != 1 ]; then
        echo expected one argument: '<'dtrace-path'>'
        exit 2
fi

dtrace=$1
t="zelda_info_t"
exe="tst.chasestrings.exe"

elfdump -c "./$exe" | grep -Fq 'sh_name: .SUNW_ctf' 
if [[ $? -ne 0 ]]; then
	echo "CTF does not exist in $exe, that's a bug" >&2
	exit 1
fi

./$exe &
pid=$!

$dtrace -64 -qs /dev/stdin <<EOF
typedef struct info {
        char    *zi_gamename;
        int     zi_ndungeons;
        char    *zi_villain;
        int     zi_haszelda;
} info_t;

pid$pid::has_princess:entry
/next == 0/
{
	this->t = (userland info_t *)arg0;
	printf("game: %s, dungeon: %d, villain: %s, zelda: %d\n",
	    stringof(this->t->zi_gamename), this->t->zi_ndungeons,
	    stringof(this->t->zi_villain), this->t->zi_haszelda);
	next = 1;
}

pid$pid::has_dungeons:entry
/next == 1/
{
	this->t = (userland info_t *)arg0;
	printf("game: %s, dungeon: %d, villain: %s, zelda: %d\n",
	    stringof(this->t->zi_gamename), this->t->zi_ndungeons,
	    stringof(this->t->zi_villain), this->t->zi_haszelda);
	next = 2;
}

pid$pid::has_villain:entry
/next == 2/
{
	this->t = (userland info_t *)arg0;
	printf("game: %s, dungeon: %d, villain: %s, zelda: %d\n",
	    stringof(this->t->zi_gamename), this->t->zi_ndungeons,
	    stringof(this->t->zi_villain), this->t->zi_haszelda);
	exit(0);
}

ERROR
{
	exit(1);
}
EOF
rc=$?

kill -9 $pid

exit $rc
