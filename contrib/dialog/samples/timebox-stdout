#!/bin/sh
# $Id: timebox-stdout,v 1.5 2010/01/13 10:37:19 tom Exp $

. ./setup-vars

RESULT=`$DIALOG --stdout --title "TIMEBOX" "$@" --timebox "Please set the time..." 0 0 12 34 56`

retval=$?

. ./report-string
