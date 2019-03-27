#!/bin/sh
# $FreeBSD$

base=`basename $0`

echo "1..2"

name="pkill -g <pgrp>"
pgrp=`ps -o tpgid -p $$ | tail -1`
sleep=$(pwd)/sleep.txt
ln -sf /bin/sleep $sleep
$sleep 5 &
sleep 0.3
pkill -f -g $pgrp $sleep
ec=$?
case $ec in
0)
	echo "ok 1 - $name"
	;;
*)
	echo "not ok 1 - $name"
	;;
esac
rm -f $sleep

name="pkill -g 0"
sleep=$(pwd)/sleep.txt
ln -sf /bin/sleep $sleep
$sleep 5 &
sleep 0.3
pkill -f -g 0 $sleep
ec=$?
case $ec in
0)
	echo "ok 2 - $name"
	;;
*)
	echo "not ok 2 - $name"
	;;
esac
rm -f $sleep
