#!/bin/sh
# $Id: timebox,v 1.9 2010/01/13 10:23:10 tom Exp $

. ./setup-vars

DIALOG_ERROR=254
export DIALOG_ERROR

exec 3>&1
RESULT=`$DIALOG --title "TIMEBOX" "$@" --timebox "Please set the time..." 0 0 12 34 56 2>&1 1>&3`
retval=$?
exec 3>&-

. ./report-string
