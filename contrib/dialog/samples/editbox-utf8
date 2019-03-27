#!/bin/sh
# $Id: editbox-utf8,v 1.9 2010/01/13 10:20:03 tom Exp $

. ./setup-vars

. ./setup-tempfile

. ./setup-utf8

. ./setup-edit

cat << EOF > $input
Hi, this is a edit box. You can use this to 
allow the user to enter or modify free-form text.

Try it now!

        -----------     --------------------------------
	Choose		Description of the OS you like
	-----------	--------------------------------
	Ｌｉｎｕｘ	The Great Unix Clone for 386/486 
	ＮｅｔＢＳＤ	Another free Unix Clone for 386/486 
	ＯＳ/２		IBM OS/2 
	ＷＩＮ ＮＴ	Microsoft Windows NT 
	ＰＣＤＯＳ	IBM PC DOS 
	ＭＳＤＯＳ	Microsoft DOS
	-----------	--------------------------------
        -----------     --------------------------------
EOF

$DIALOG --title "EDIT BOX" \
	--fixed-font "$@" --editbox $input 0 0 2>$output
retval=$?

. ./report-edit
