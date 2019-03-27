#!/bin/sh
# $Id: calendar2,v 1.8 2010/01/13 10:20:03 tom Exp $

. ./setup-vars

exec 3>&1
RESULT=`$DIALOG --title "CALENDAR" "$@" --calendar "Please choose a date..." 0 0 2>&1 1>&3`
retval=$?
exec 3>&-

. ./report-string
