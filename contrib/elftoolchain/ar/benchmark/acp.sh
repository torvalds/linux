#!/bin/sh
# $Id: acp.sh 2086 2011-10-27 05:18:01Z jkoshy $

# This script is adapted from Jan Psota's Tar Comparison Program(TCP).

n=3			# number of repetitions
AR="bsdar gnuar"	# ar archivers to compare

test $# -ge 2 || {
    echo "usage: $0 source_dir where_to_place_archive [where_to_extract_it]"
    exit 0
}

THISDIR=`/bin/pwd`
src=$1
dst=$2/acp.a
ext=${3:-$2}/acptmp
test -e $dst -o -e /tmp/acp \
    && { echo "$dst or /tmp/acp exists, exiting"; exit 1; }
mkdir -p $ext || exit 1

show_result ()
{
    awk -vL="`du -k $dst`" '{printf "%s\t%s\t%s\%10.1d KB/s\n",
$1, $3, $5, ($1>0?L/$1:0)}' /tmp/acp | sort | head -n 1
}

test -d $src || { echo "'$src' is not a directory"; exit 1; }

# ar versions
for ar in $AR; do echo -n "$ar:	"; $ar -V | head -n 1;
done

echo
echo "best time of $n repetitions"
echo -n "		src=$src, "
echo -n "`du -sh $src | awk '{print $1}'`"
echo -n " in "
echo "`find $src | wc -l` files"
echo "		archive=$dst, extract to $ext"

echo "program	operation	real	user	system	   speed"
for op in "cru $dst $src/*" "t $dst" "x `basename $dst`"; do
    for ar in $AR; do
	echo -n "$ar	"
	echo $op | grep -q ^cr && echo -n "create		"
	echo $op | grep -q ^t && echo -n "list		"
	echo $op | grep -q ^x && echo -n "extract		"
	num=0
	while [ $num -lt $n ]; do
	    echo $op | grep -q ^cr && rm -f $dst
	    echo $op | grep -q ^x && { rm -rf $ext; mkdir -p $ext
		cp $dst $ext; cd $ext; }
	    sync
	    time $ar $op > /dev/null 2>> /tmp/acp
	    echo $op | grep -q ^x && cd $THISDIR
	    num=`expr $num + 1`
	done
	show_result
	rm -rf /tmp/acp
    done
    echo
done
rm -rf $ext $dst
rm -f /tmp/acp
