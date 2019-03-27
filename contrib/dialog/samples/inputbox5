#!/bin/sh
# $Id: inputbox5,v 1.7 2010/01/13 10:20:03 tom Exp $
# use --output-fd to write to a different output than stderr

. ./setup-vars

. ./setup-tempfile

$DIALOG --title "INPUT BOX" --clear --output-fd 4 "$@" \
        --inputbox "Hi, this is an input dialog box. You can use \n
this to ask questions that require the user \n
to input a string as the answer. You can \n
input strings of length longer than the \n
width of the input box, in that case, the \n
input field will be automatically scrolled. \n
You can use BACKSPACE to correct errors. \n\n
Try entering your name below:" 16 51 4> $tempfile

retval=$?

. ./report-tempfile
