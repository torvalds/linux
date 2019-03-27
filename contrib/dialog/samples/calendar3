#!/bin/sh
# $Id: calendar3,v 1.9 2010/01/13 10:20:03 tom Exp $

. ./setup-vars

exec 3>&1
RESULT=`$DIALOG --extra-button --extra-label "Hold" --help-button --title "CALENDAR" "$@" --calendar "Please choose a date..." 0 0 7 7 1981 2>&1 1>&3`
retval=$?
exec 3>&-

. ./report-string
