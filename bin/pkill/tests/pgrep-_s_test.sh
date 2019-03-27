#!/bin/sh
# $FreeBSD$

base=`basename $0`

echo "1..2"

exp_pid="$(ps ax | grep '\[idle\]' | awk '{print $1}')"

name="pgrep -S"
pid=`pgrep -Sx idle`
if [ "$pid" = "$exp_pid" ]; then
	echo "ok 1 - $name"
else
	echo "not ok 1 - $name"
fi
pid=`pgrep -x idle`
if [ "$pid" != "$exp_pid" ]; then
	echo "ok 2 - $name"
else
	echo "not ok 2 - $name"
fi
