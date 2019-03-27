#! /bin/sh
# $Id: inputmenu3,v 1.11 2012/07/01 01:00:34 tom Exp $
#
# "inputmenu1" with defaultitem, help-button and item-help.

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
	  --default-item "$defaultitem" \
	  --item-help "$@" \
	  --inputmenu "Originally I designed --inputmenu for a \
configuration purpose. Here is a possible piece of a configuration program." \
20 60 10 \
	"Username:"	"$user" "User login-name" \
	"UID:"		"$uid"  "User-ID (number)" \
	"GID:"		"$gid"  "Group-ID (number)" \
	"HOME:"		"$home" "User's home-directory" \
2>&1 1>&3`
returncode=$?
exec 3>&-

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
		case $value in
		HELP*)
			"$DIALOG" \
			--textbox "$0" 0 0
			;;
		*)
			"$DIALOG" \
			--clear \
			--backtitle "$backtitle" \
			--msgbox "useradd \n\
				-d $home \n\
				-u $uid \n\
				-g $gid \n\
				$user" 10 40
			;;
		esac
		;;
	$DIALOG_HELP)
		"$DIALOG" \
		--textbox "$0" 0 0
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
