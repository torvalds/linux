#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
dir=$(dirname $0)
CC=$1
OBJDUMP=$2
tmp=${TMPDIR:-/tmp}
out=$tmp/out$$.o
$CC -c $dir/check-gas-asm.S -o $out
res=$($OBJDUMP -r --section .data $out | fgrep 00004 | tr -s ' ' |cut -f3 -d' ')
rm -f $out
if [ $res != ".text" ]; then
	echo buggy
else
	echo good
fi
exit 0
