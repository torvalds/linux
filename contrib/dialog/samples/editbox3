#!/bin/sh
# $Id: editbox3,v 1.7 2010/01/13 10:20:03 tom Exp $
# example with extra- and help-buttons

. ./setup-vars

. ./setup-edit

cat << EOF > $input
EOF

$DIALOG --title "EDIT BOX" \
	--extra-button \
	--help-button \
	--fixed-font "$@" --editbox $input 0 0 2>$output
retval=$?

. ./report-edit
