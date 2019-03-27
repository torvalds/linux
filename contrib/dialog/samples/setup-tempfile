#!/bin/sh
# $Id: setup-tempfile,v 1.4 2016/01/26 22:42:47 tom Exp $
# vile:shmode

tempfile=`(tempfile) 2>/dev/null` || tempfile=/tmp/test$$
trap "rm -f $tempfile" 0 $SIG_NONE $SIG_HUP $SIG_INT $SIG_QUIT $SIG_TERM
