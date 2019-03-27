#!/bin/sh
# $Id: rangebox4,v 1.1 2012/12/05 11:54:04 tom Exp $

. ./setup-vars

exec 3>&1
RESULT=`$DIALOG --title "RANGE BOX" --rangebox "Please set the volume..." 0 60 10 100 5 2>&1 1>&3`
retval=$?
exec 3>&-

. ./report-string
