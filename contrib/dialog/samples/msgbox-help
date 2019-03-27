#!/bin/sh
# $Id: msgbox-help,v 1.6 2010/01/13 10:53:11 tom Exp $

. ./setup-vars

$DIALOG --title "MESSAGE BOX" --clear \
	--help-button "$@" \
        --msgbox "Hi, this is a simple message box. You can use this to \
                  display any message you like. The box will remain until \
                  you press the ENTER key." 10 41

retval=$?

. ./report-button
