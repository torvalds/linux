#!/bin/sh
# $Id: pause-both,v 1.1 2011/01/18 09:49:24 tom Exp $

. ./setup-vars

$DIALOG --title "PAUSE" \
	--help-button \
	--extra-button "$@" \
	--pause "Hi, this is a pause widget" 20 70 10

retval=$?
echo return $retval

. ./report-button
