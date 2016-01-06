#!/bin/sh
########################################################################### ###
#@File
#@Title         Test the nature of the C compiler.
#@Copyright     Copyright (c) Imagination Technologies Ltd. All Rights Reserved
#@License       Dual MIT/GPLv2
# 
# The contents of this file are subject to the MIT license as set out below.
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# Alternatively, the contents of this file may be used under the terms of
# the GNU General Public License Version 2 ("GPL") in which case the provisions
# of GPL are applicable instead of those above.
# 
# If you wish to allow use of your version of this file only under the terms of
# GPL, and not to allow others to use your version of this file under the terms
# of the MIT license, indicate your decision by deleting the provisions above
# and replace them with the notice and other provisions required by GPL as set
# out in the file called "GPL-COPYING" included in this distribution. If you do
# not delete the provisions above, a recipient may use your version of this file
# under the terms of either the MIT license or GPL.
# 
# This License is also included in this distribution in the file called
# "MIT-COPYING".
# 
# EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
# PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
# BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
# PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
### ###########################################################################

LANG=C
export LANG

usage() {
	echo "usage: $0 [--64] [--clang] --cc CC [--out OUT] [cflag]"
	exit 1
}

check_clang() {
	$CC -Wp,-dM -E - </dev/null | grep __clang__ >/dev/null 2>&1
	if [ "$?" = "0" ]; then
		# Clang must be passed a program with a main() that returns 0.
		# It will produce an error if main() is improperly specified.
		IS_CLANG=1
		TEST_PROGRAM="int main(void){return 0;}"
	else
		# If we're not clang, assume we're GCC. GCC needs to be passed
		# a program with a faulty return in main() so that another
		# warning (unrelated to the flag being tested) is emitted.
		# This will cause GCC to warn about the unsupported warning flag.
		IS_CLANG=0
		TEST_PROGRAM="int main(void){return;}"
	fi
}

do_cc() {
	echo "$TEST_PROGRAM" 2> /dev/null | $CC -W -Wall $3 -xc -c - -o $1 >$2 2>&1
}

while [ 1 ]; do
	if [ "$1" = "--64" ]; then
		[ -z $CLANG ] && BIT_CHECK=1
	elif [ "$1" = "--clang" ]; then
		[ -z $BIT_CHECK ] && CLANG=1
	elif [ "$1" = "--cc" ]; then
		[ "x$2" = "x" ] && usage
		CC="$2" && shift
	elif [ "$1" = "--out" ]; then
		[ "x$2" = "x" ] && usage
		OUT="$2" && shift
	elif [ "${1#--}" != "$1" ]; then
		usage
	else
		break
	fi
	shift
done

[ "x$CC" = "x" ] && usage
[ "x$CLANG" = "x" -a "x$OUT" = "x" ] && usage
ccof=$OUT/cc-sanity-check
log=${ccof}.log

check_clang

if [ "x$BIT_CHECK" = "x1" ]; then
	do_cc $ccof $log ""
	file $ccof | grep 64-bit >/dev/null 2>&1
	[ "$?" = "0" ] && echo true || echo false
elif [ "x$CLANG" = "x1" ]; then
	[ "x$IS_CLANG" = "x1" ] && echo true || echo false
else
	[ "x$1" = "x" ] && usage
	do_cc $ccof $log $1
	if [ "$?" = "0" ]; then
		# compile passed, but was the warning unrecognized?
		if [ "x$IS_CLANG" = "x1" ]; then
			grep "^warning: unknown warning option '$1'" $log >/dev/null 2>&1
		else
			grep -E "(^cc1: warning: unrecognized command line option \"$1\"|^cc1: warning: command line option \"$1\" is valid for C\+\+/ObjC\+\+ but not for C|gcc: unrecognized option '$1')" $log >/dev/null 2>&1
		fi
		[ "$?" = "1" ] && echo $1
	fi
fi

rm -f $ccof $log
exit 0
