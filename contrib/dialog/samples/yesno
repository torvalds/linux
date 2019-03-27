#!/bin/sh
# $Id: yesno,v 1.9 2010/01/14 01:11:11 tom Exp $

. ./setup-vars

DIALOG_ERROR=254
export DIALOG_ERROR

$DIALOG --title "YES/NO BOX" --clear "$@" \
        --yesno "Hi, this is a yes/no dialog box. You can use this to ask \
                 questions that have an answer of either yes or no. \
                 BTW, do you notice that long lines will be automatically \
                 wrapped around so that they can fit in the box? You can \
                 also control line breaking explicitly by inserting \
                 'backslash n' at any place you like, but in this case, \
                 auto wrap around will be disabled and you will have to \
                 control line breaking yourself." 15 61

retval=$?

. ./report-yesno
