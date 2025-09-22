#!/bin/sh

if [ $# -lt 1 ]; then
	destdir=/
	echo 'warning: using DESTDIR=/'
else
	destdir=$1
fi

(
	cd $destdir/usr/share/zoneinfo
	ls -1dF `tar cvf /dev/null [A-Za-y]*`
) > var/tzlist
