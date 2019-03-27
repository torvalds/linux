#! /bin/sh
# $Id: inputmenu2,v 1.9 2012/07/01 01:00:34 tom Exp $
#
# "inputmenu1" with defaultitem, help-button.

. ./setup-vars

backtitle="An Example for the use of --inputmenu:"

ids=`id|sed -e 's/([^)]*)//g'`
uid=`echo "$ids" | sed -e 's/^uid=//' -e 's/ .*//'`
gid=`echo "$ids" | sed -e 's/^.* gid=//' -e 's/ .*//'`

user="$USER"
home="$HOME"

returncode=0
defaultitem="Username:"
while test $returncode != 1 && test $returncode != 250
do
exec 3>&1
value=`$DIALOG --clear --ok-label "Create" \
	  --backtitle "$backtitle" \
	  --help-button \
	  --help-label "Script" \
	  --default-item "$defaultitem" "$@" \
	  --inputmenu "Originally I designed --inputmenu for a \
configuration purpose. Here is a possible piece of a configuration program." \
20 60 10 \
	"Username:"	"$user" \
	"UID:"		"$uid"  \
	"GID:"		"$gid"  \
	"HOME:"		"$home" \
2>&1 1>&3`
returncode=$?
exec 3>&-

	case $returncode in
	$DIALOG_HELP)
		"$DIALOG" \
		--textbox "$0" 0 0
		;;
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
		--backtitle "$backtitle" \
		--msgbox "useradd \n\
			-d $home \n\
			-u $uid \n\
			-g $gid \n\
			$user" 10 40
		;;
	$DIALOG_EXTRA)
		tag=`echo "$value" |sed -e 's/^RENAMED //' -e 's/:.*/:/'`
		item=`echo "$value" |sed -e 's/^[^:]*:[ ]*//' -e 's/[ ]*$//'`

		case "$tag" in
		Username:)
			user="$item"
			;;
		UID:)
			uid="$item"
			;;
		GID:)
			gid="$item"
			;;
		HOME:)
			home="$item"
			;;
		*)
			tag=
			;;
		esac
		test -n "$tag" && defaultitem="$tag"
		;;

	$DIALOG_ESC)
                echo "ESC pressed."
                break
                ;;

	esac
done
