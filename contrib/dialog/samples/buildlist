#!/bin/sh
# $Id: buildlist,v 1.2 2012/12/04 11:45:21 tom Exp $

. ./setup-vars

. ./setup-tempfile

$DIALOG --title "BUILDLIST DEMO" --backtitle "A user-built list" \
	--separator "|" \
	--buildlist "hello, this is a --buildlist..." 0 0 0 \
		"1" "Item number 1" "on" \
		"2" "Item number 2" "off" \
		"3" "Item number 3" "on" \
		"4" "Item number 4" "on" \
		"5" "Item number 5" "off" \
		"6" "Item number 6" "on" 2> $tempfile

retval=$?

. ./report-tempfile
