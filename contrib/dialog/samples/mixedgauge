#! /bin/sh
# $Id: mixedgauge,v 1.7 2010/01/13 10:20:03 tom Exp $

. ./setup-vars

background="An Example of --mixedgauge usage"

for i in 5 10 20 30 40 50 60 70 80 90 100
do
$DIALOG --backtitle "$background" \
	--title "Mixed gauge demonstration" "$@" \
	--mixedgauge "This is a prompt message,\nand this is the second line." \
		0 0 33 \
		"Process one"	"0" \
		"Process two"	"1" \
		"Process three"	"2" \
		"Process four"	"3" \
		""		"8" \
		"Process five"	"5" \
		"Process six"	"6" \
		"Process seven"	"7" \
		"Process eight"	"4" \
		"Process nine"	"-$i"
# break
sleep 1 
done
