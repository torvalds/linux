#!/bin/sh
# $FreeBSD$

base=`basename $0`

echo "1..2"

name="pgrep -LF <pidfile>"
pidfile=$(pwd)/pidfile.txt
sleep=$(pwd)/sleep.txt
ln -sf /bin/sleep $sleep
daemon -p $pidfile $sleep 5
sleep 0.3
chpid=`cat $pidfile`
pid=`pgrep -f -L -F $pidfile $sleep`
if [ "$pid" = "$chpid" ]; then
	echo "ok 1 - $name"
else
	echo "not ok 1 - $name"
fi
kill "$chpid"

# Be sure we cannot find process which pidfile is not locked.
$sleep 5 &
sleep 0.3
chpid=$!
echo $chpid > $pidfile
pgrep -f -L -F $pidfile $sleep 2>/dev/null
ec=$?
case $ec in
0)
	echo "not ok 2 - $name"
	;;
*)
	echo "ok 2 - $name"
	;;
esac

kill "$chpid"
rm -f $pidfile
rm -f $sleep
