#!/bin/sh
# $Id: setup-edit,v 1.3 2016/01/26 22:42:44 tom Exp $
# vile:shmode

input=`tempfile 2>/dev/null` || input=/tmp/input$$
output=`tempfile 2>/dev/null` || output=/tmp/test$$
trap "rm -f $input $output" $SIG_NONE $SIG_HUP $SIG_INT $SIG_QUIT $SIG_TERM
