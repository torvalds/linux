#!/bin/sh
# $FreeBSD$

base=`basename $0`

echo "1..2"

name="pkill -t <tty>"
tty=`ps -x -o tty -p $$ | tail -1`
if [ "$tty" = "??" -o "$tty" = "-" ]; then
	tty="-"
	ttyshort="-"
else
	case $tty in
	pts/*)	ttyshort=`echo $tty | cut -c 5-` ;;
	*)	ttyshort=`echo $tty | cut -c 4-` ;;
	esac
fi
sleep=$(pwd)/sleep.txt
ln -sf /bin/sleep $sleep
$sleep 5 &
sleep 0.3
pkill -f -t $tty $sleep
ec=$?
case $ec in
0)
	echo "ok 1 - $name"
	;;
*)
	echo "not ok 1 - $name"
	;;
esac
$sleep 5 &
sleep 0.3
pkill -f -t $ttyshort $sleep
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
