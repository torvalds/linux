#!/bin/sh
# $Id: inputbox1,v 1.13 2010/01/13 10:20:03 tom Exp $

. ./setup-vars

. ./setup-tempfile

$DIALOG --cr-wrap \
	--title "INPUT BOX" --clear \
        --inputbox "$@" \
"Hi, this is an input dialog box. You can use
this to ask questions that require the user
to input a string as the answer. You can 
input strings of length longer than the 
width of the input box, in that case, the 
input field will be automatically scrolled. 
You can use BACKSPACE to correct errors. 

Try entering your name below:" 0 0 2> $tempfile

retval=$?

. ./report-tempfile
