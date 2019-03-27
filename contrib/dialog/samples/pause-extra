#!/bin/sh
# $Id: pause-extra,v 1.1 2011/01/18 09:49:07 tom Exp $

. ./setup-vars

$DIALOG --title "PAUSE" \
	--extra-button "$@" \
	--pause "Hi, this is a pause widget" 20 70 10

retval=$?
echo return $retval

. ./report-button
