#!/bin/sh
# $Id: rangebox3,v 1.1 2012/12/05 10:19:42 tom Exp $

. ./setup-vars

exec 3>&1
RESULT=`$DIALOG --title "RANGE BOX" --rangebox "Please set the volume..." 0 60 -48 55 5 2>&1 1>&3`
retval=$?
exec 3>&-

. ./report-string
