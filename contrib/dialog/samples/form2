#! /bin/sh
# $Id: form2,v 1.9 2010/01/13 10:53:11 tom Exp $

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
	  --backtitle "$backtitle" "$@" \
	  --form "Here is a possible piece of a configuration program." \
0 0 10 \
	"1 Username:"  1 1	"$user" 1  12 10 10 \
	"1 UID:"       2 1	"$uid"  2  12 10 10 \
	"1 GID:"       3 1	"$gid"  3  12 10 10 \
	"1 HOME:"      4 1	"$home" 4  12 10 10 \
	"2 Username:"  5 1	"$user" 5  12 10 10 \
	"2 UID:"       6 1	"$uid"  6  12 10 10 \
	"2 GID:"       7 1	"$gid"  7  12 10 10 \
	"2 HOME:"      8 1	"$home" 8  12 10 10 \
	"3 Username:"  9 1	"$user" 9  12 10 10 \
	"3 UID:"      10 1	"$uid"  10 12 10 10 \
	"3 GID:"      11 1	"$gid"  11 12 10 10 \
	"3 HOME:"     12 1	"$home" 12 12 10 10 \
	"4 Username:" 13 1	"$user" 13 12 10 10 \
	"4 UID:"      14 1	"$uid"  14 12 10 10 \
	"4 GID:"      15 1	"$gid"  15 12 10 10 \
	"4 HOME:"     16 1	"$home" 16 12 10 10 \
	"5 Username:" 17 1	"$user" 17 12 10 10 \
	"5 UID:"      18 1	"$uid"  18 12 10 10 \
	"5 GID:"      19 1	"$gid"  19 12 10 10 \
	"5 HOME:"     20 1	"$home" 20 12 10 10 \
	"6 Username:" 21 1	"$user" 21 12 10 10 \
	"6 UID:"      22 1	"$uid"  22 12 10 10 \
	"6 GID:"      23 1	"$gid"  23 12 10 10 \
	"6 HOME:"     24 1	"$home" 24 12 10 10 \
	"7 Username:" 25 1	"$user" 25 12 10 10 \
	"7 UID:"      26 1	"$uid"  26 12 10 10 \
	"7 GID:"      27 1	"$gid"  27 12 10 10 \
	"7 HOME:"     28 1	"$home" 28 12 10 10 \
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
$show" 0 0
		;;
	*)
		echo "Return code was $returncode"
		exit
		;;
	esac
done
