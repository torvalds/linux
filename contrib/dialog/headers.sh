#! /bin/sh
# $Id: headers.sh,v 1.3 2007/02/25 20:37:56 tom Exp $
##############################################################################
# Copyright (c) 2004,2007 Thomas E. Dickey                                   #
#                                                                            #
# Permission is hereby granted, free of charge, to any person obtaining a    #
# copy of this software and associated documentation files (the "Software"), #
# to deal in the Software without restriction, including without limitation  #
# the rights to use, copy, modify, merge, publish, distribute, distribute    #
# with modifications, sublicense, and/or sell copies of the Software, and to #
# permit persons to whom the Software is furnished to do so, subject to the  #
# following conditions:                                                      #
#                                                                            #
# The above copyright notice and this permission notice shall be included in #
# all copies or substantial portions of the Software.                        #
#                                                                            #
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR #
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   #
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    #
# THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER      #
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    #
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        #
# DEALINGS IN THE SOFTWARE.                                                  #
#                                                                            #
# Except as contained in this notice, the name(s) of the above copyright     #
# holders shall not be used in advertising or otherwise to promote the sale, #
# use or other dealings in this Software without prior written               #
# authorization.                                                             #
##############################################################################
#
# Adjust includes for header files that reside in a subdirectory of
# /usr/include, etc.
#
# Parameters (the first case creates the sed script):
#	$1 is the target directory
#	$2 is the source directory
# or (the second case does the install, using the sed script):
#	$1 is the script to use for installing
#	$2 is the target directory
#	$3 is the source directory
#	$4 is the file to install, editing source/target/etc.

PACKAGE=DIALOG
PKGNAME=DLG
CONFIGH=dlg_config.h

TMPSED=headers.sed

if test $# = 2 ; then
	rm -f $TMPSED
	DST=$1
	REF=$2
	LEAF=`basename $DST`
	case $DST in
	/*/include/$LEAF)
		END=`basename $DST`
		for i in $REF/*.h
		do
			NAME=`basename $i`
			echo "s/<$NAME>/<$END\/$NAME>/g" >> $TMPSED
		done
		;;
	*)
		echo "" >> $TMPSED
		;;
	esac
	for name in `
	egrep '^#define[ 	][ 	]*[_ABCDEFGHIJKLMNOPQRSTUVWXYZ]' $REF/$CONFIGH \
		| sed	-e 's/^#define[ 	][ 	]*//' \
			-e 's/[ 	].*//' \
		| egrep -v "^${PACKAGE}_" \
		| sort -u \
		| egrep -v "^${PKGNAME}_"`
	do
		echo "s/\\<$name\\>/${PKGNAME}_$name/g" >>$TMPSED
	done
else
	PRG=""
	while test $# != 3
	do
		PRG="$PRG $1"; shift
	done

	DST=$1
	REF=$2
	SRC=$3

	SHOW=`basename $SRC`
	TMPSRC=${TMPDIR-/tmp}/${SHOW}$$

	echo "	... $SHOW"
	test -f $REF/$SRC && SRC="$REF/$SRC"

	rm -f $TMPSRC
	sed -f $TMPSED $SRC > $TMPSRC
	NAME=`basename $SRC`

	# Just in case someone gzip'd manpages, remove the conflicting copy.
	test -f $DST/$NAME.gz && rm -f $DST/$NAME.gz

	eval $PRG $TMPSRC $DST/$NAME
	rm -f $TMPSRC
fi
