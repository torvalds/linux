#!/bin/sh
# $Id: yesno4,v 1.1 2010/01/14 10:26:40 tom Exp $

. ./setup-vars

$DIALOG --title "YES/NO BOX" "$@" \
        --yesno "Hi, this is a yes/no dialog box. You can use this to ask \
questions that have an answer of either yes or no. \
BTW, do you notice that long lines will be automatically \
wrapped around so that they can fit in the box? You can \
also control line breaking explicitly by inserting \
'backslash n' at any place you like, but in this case, \
auto wrap around will be disabled and you will have to \
control line breaking yourself." 6 30

retval=$?

. ./report-yesno
