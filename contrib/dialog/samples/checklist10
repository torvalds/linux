#! /bin/sh
# $Id: checklist10,v 1.9 2010/01/13 10:20:03 tom Exp $
# zero-width column

. ./setup-vars

. ./setup-tempfile

$DIALOG \
	--backtitle "No such organization" \
	--title "CHECKLIST BOX" "$@" \
        --checklist "Hi, this is a checklist box. You can use this to \n\
present a list of choices which can be turned on or \n\
off. If there are more items than can fit on the \n\
screen, the list will be scrolled. You can use the \n\
UP/DOWN arrow keys, the first letter of the choice as a \n\
hot key, or the number keys 1-9 to choose an option. \n\
Press SPACE to toggle an option on/off. \n\n\
  Which of the following are fruits?" 20 61 5 \
	Dialog		"" on \
	Readline	"" off \
	Gnome		"" off \
	Kde		"" off \
	Editor		"" off \
	Noninteractive	"" on \
        2> $tempfile

retval=$?

. ./report-tempfile
