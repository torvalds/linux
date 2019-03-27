#!/bin/sh
# $Id: calendar3-stdout,v 1.6 2010/01/13 10:20:03 tom Exp $

. ./setup-vars

RESULT=`$DIALOG --extra-button --extra-label "Hold" --help-button --stdout --title "CALENDAR" "$@" --calendar "Please choose a date..." 0 0 7 7 1981`
retval=$?

. ./report-string
