#! /bin/sh
# $Id: mixedform,v 1.6 2010/01/13 10:47:35 tom Exp $

. ./setup-vars

backtitle="An Example for the use of --mixedform:"
returncode=0
while test $returncode != 1 && test $returncode != 250
do
returncode=$?
exec 3>&1
value=`$DIALOG  --title "Mixed form demonstration" --ok-label "Submit" \
           --backtitle "$backtitle" \
	  --insecure "$@" \
	  --mixedform "Here is a possible piece of a configuration program." \
20 50 0 \
	"Username        :" 1 1	"Kiran" 1 20 10 0 2 \
	"Password        :"      2 1	""  2 20  8 0 1 \
	"Retype Password :"      3 1	""  3 20  8 0 1 \
	"HOME            :"     4 1	"Cisco" 4 20 40 0 0 \
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
		echo "Button 2 (Help) pressed."
		exit
		;;
	$DIALOG_EXTRA)
		echo "Button 3 (Extra) pressed."
		exit
		;;
	*)
		echo "Return code was $returncode"
		exit
		;;
	esac
done
