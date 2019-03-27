#!/bin/sh
# $Id: menubox-utf8,v 1.10 2010/01/13 10:20:03 tom Exp $

. ./setup-vars

. ./setup-tempfile

. ./setup-utf8

$DIALOG --clear --title "MENU BOX" "$@" \
        --menu "Hi, this is a menu box. You can use this to \n\
present a list of choices for the user to \n\
choose. If there are more items than can fit \n\
on the screen, the menu will be scrolled. \n\
You can use the UP/DOWN arrow keys, the first \n\
letter of the choice as a hot key, or the \n\
number keys 1-9 to choose an option.\n\
Try it now!\n\n\
	Choose the OS you like:" 20 51 4 \
	"Ｌｉｎｕｘ"	"The Great Unix Clone for 386/486" \
	"ＮｅｔＢＳＤ"	"Another free Unix Clone for 386/486" \
	"ＯＳ/２"	"IBM OS/2" \
	"ＷＩＮ ＮＴ"	"Microsoft Windows NT" \
	"ＰＣＤＯＳ"	"IBM PC DOS" \
	"ＭＳＤＯＳ"	"Microsoft DOS" 2> $tempfile

retval=$?

. ./report-tempfile
