#!/bin/sh
# $Id: fselect0,v 1.1 2011/10/14 08:32:48 tom Exp $

. ./setup-vars

exec 3>&1
RESULT=`$DIALOG --title "Please choose a file" "$@" --fselect '' 14 48 2>&1 1>&3`
retval=$?
exec 3>&-

. ./report-string
