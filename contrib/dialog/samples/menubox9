#!/bin/sh
# $Id: menubox9,v 1.6 2010/01/13 10:20:03 tom Exp $

. ./setup-vars

exec 3>&1
RESULT=`$DIALOG --help-button \
	--clear \
	--title "Select Linux installation partition:" "$@" \
	--menu \
"Please select a partition from the following list to use for your \
root (/) Linux partition." 13 70 5 \
"/dev/hda2" "Linux native 30724312K" \
"/dev/hda4" "Linux native 506047K" \
"/dev/hdb1" "Linux native 4096543K" \
"/dev/hdb2" "Linux native 2586465K" \
"---" "(add none, continue with setup)" \
"---" "(add none, continue with setup)" \
"---" "(add none, continue with setup)" \
"---" "(add none, continue with setup)" \
"---" "(add none, continue with setup)" \
2>&1 1>&3`
retval=$?
exec 3>&-

. ./report-string
