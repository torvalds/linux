#!/bin/sh

EVENTS=`grep gator_events_init $2/*.c | sed 's/.\+gator_events_init(\(.\+\)).\+/\1/'`

(
	echo /\* This file is auto generated \*/
	echo
	for EVENT in $EVENTS; do
		echo __weak int $EVENT\(void\)\;
	done
	echo
	echo static int \(*gator_events_list[]\)\(void\) = {
	for EVENT in $EVENTS; do
		echo \	$EVENT,
	done
	echo }\;
) > $2/$1.tmp

cmp -s $2/$1 $2/$1.tmp && rm $2/$1.tmp || mv $2/$1.tmp $2/$1
