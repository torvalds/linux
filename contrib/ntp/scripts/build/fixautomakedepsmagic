#!/bin/sh

prog=`basename $0`


t=/tmp/$prog.$$

trap 'rm -f ${t} ; exit 1' 1 3 15

while [ $# -gt 0 ]; do
        f=$1
	shift
	sed -e '/^DEPS_MAGIC :=/,/^-include \$/s/^/#/' $f > $t
	c="diff $f $t"
	echo $c
	$c
	tstatus=$?
	if [ $tstatus = 0 ]; then
		echo "$prog":" $f not modified"
	elif [ ! -w $f ]; then
		echo "$prog":" $f not not writable"
	else
		c="cp $t $f"
		echo $c
		$c
	fi
	rm -f $t
done
