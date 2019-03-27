#! /bin/sh
# $Id: form6,v 1.5 2010/01/13 10:49:52 tom Exp $
# form4 with --help-status

. ./setup-vars

backtitle="An Example for the use of --form:"

ids=`id|sed -e 's/([^)]*)//g'`
uid=`echo "$ids" | sed -e 's/^uid=//' -e 's/ .*//'`
gid=`echo "$ids" | sed -e 's/^.* gid=//' -e 's/ .*//'`

user="$USER"
home="$HOME"

returncode=0
while test $returncode != 1 && test $returncode != 250
do
exec 3>&1
value=`$DIALOG --ok-label "Submit" \
	  --help-status \
	  --help-button \
	  --item-help \
	  --backtitle "$backtitle" "$@" \
	  --form "Here is a possible piece of a configuration program." \
20 50 0 \
	"Username:" 1 1	"$user" 1 10 -9 9 "Login name" \
	"UID:"      2 1	"$uid"  2 10  8 0 "User ID" \
	"GID:"      3 1	"$gid"  3 10  8 0 "Group ID" \
	"HOME:"     4 1	"$home" 4 10 40 0 "User's home-directory" \
2>&1 1>&3`
returncode=$?
exec 3>&-

show=`echo "$value" |sed -e 's/^/	/'`

	case $returncode in
	$DIALOG_CANCEL)
		"$DIALOG" \
		--clear \
		--backtitle "$backtitle" \
		--yesno "Really quit?" 10 30
		case $? in
		$DIALOG_OK)
			break
			;;
		$DIALOG_CANCEL)
			returncode=99
			;;
		esac
		;;
	$DIALOG_OK)
		"$DIALOG" \
		--clear \
		--backtitle "$backtitle" --no-collapse --cr-wrap \
		--msgbox "Resulting data:\n\
$show" 10 40
		;;
	$DIALOG_HELP)
		"$DIALOG" \
		--clear \
		--backtitle "$backtitle" --no-collapse --cr-wrap \
		--msgbox "Help data:\n\
$show" 10 40
		;;
	*)
		echo "Return code was $returncode"
		exit
		;;
	esac
done
