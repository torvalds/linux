#!/bin/sh
# $Id: timebox2,v 1.7 2010/01/13 10:37:35 tom Exp $

. ./setup-vars

exec 3>&1
RESULT=`$DIALOG --title "TIMEBOX" "$@" --timebox "Please set the time..." 0 0 2>&1 1>&3`
retval=$?
exec 3>&-

. ./report-string
