#!/bin/sh
# $Id: run_test.sh,v 1.4 2017/02/01 01:50:09 tom Exp $
# vile:ts=4 sw=4
THIS=`basename $0`

if [ -z "$DIALOG" ]
then
	have=
	want=dialog
	for p in . .. ../bin
	do
		prog=$p/$want
		[ -f $prog ] || continue
		if [ -x $prog ]
		then
			have=$prog
			break
		fi
	done

	if [ -z "$have" ]
	then
		echo "? did not find $want" >&2
		exit
	fi

	DIALOG=$have
	export DIALOG
fi

want=`basename $DIALOG`

DIALOGOPTS="$DIALOGOPTS --trace $want.log"
export DIALOGOPTS

mylog=run_test.log
cat >$mylog <<EOF
** `date`
EOF

for name in "$@"
do
	[ -f "$name" ] || continue
	[ -x "$name" ] || continue
	# skip this script and known utility-scripts
	case `basename $name` in
	$THIS|dft-*|killall|listing|rotated-data|shortlist|with-*)
		echo "** skipping $name" >>$mylog
		continue
		;;
	esac
	rm -f trace $want.log $name.log
	echo "** running $name" >>$mylog
	$name
	[ -f $want.log ] && cp $want.log $name.log
done
