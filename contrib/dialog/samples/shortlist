#!/bin/sh
# $Id: shortlist,v 1.2 2011/03/02 00:11:50 tom Exp $
# make a short listing, which writes to both stdout and stderr.

if test $# != 0
then
	count=$1
else
	count=10
fi

while test $count != 0
do
	echo "** $count -- `date`"
	w >&2
	sleep 1
	count=`expr $count - 1 2>/dev/null`
	test -z "$count" && count=0
done
