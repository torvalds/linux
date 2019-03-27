#!/bin/sh
# $Id: rangebox,v 1.3 2012/12/05 10:07:54 tom Exp $

. ./setup-vars

exec 3>&1
RESULT=`$DIALOG --title "RANGE BOX" --rangebox "Please set the volume..." 0 60 0 123 5 2>&1 1>&3`
retval=$?
exec 3>&-

. ./report-string
