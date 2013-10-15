#!/bin/sh

EVENTS=`grep gator_events_init *.c | sed 's/.\+gator_events_init(\(.\+\)).\+/\1/'`

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
) > $1.tmp

cmp -s $1 $1.tmp && rm $1.tmp || mv $1.tmp $1
