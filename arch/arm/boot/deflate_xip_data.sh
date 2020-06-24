#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# XIP kernel .data segment compressor
#
# Created by:	Nicolas Pitre, August 2017
# Copyright:	(C) 2017  Linaro Limited
#

# This script locates the start of the .data section in xipImage and
# substitutes it with a compressed version. The needed offsets are obtained
# from symbol addresses in vmlinux. It is expected that .data extends to
# the end of xipImage.

set -e

VMLINUX="$1"
XIPIMAGE="$2"

DD="dd status=none"

# Use "make V=1" to debug this script.
case "$KBUILD_VERBOSE" in
*1*)
	set -x
	;;
esac

sym_val() {
	# extract hex value for symbol in $1
	local val=$($NM "$VMLINUX" 2>/dev/null | sed -n "/ $1\$/{s/ .*$//p;q}")
	[ "$val" ] || { echo "can't find $1 in $VMLINUX" 1>&2; exit 1; }
	# convert from hex to decimal
	echo $((0x$val))
}

__data_loc=$(sym_val __data_loc)
_edata_loc=$(sym_val _edata_loc)
base_offset=$(sym_val _xiprom)

# convert to file based offsets
data_start=$(($__data_loc - $base_offset))
data_end=$(($_edata_loc - $base_offset))

# Make sure data occupies the last part of the file.
file_end=$(${CONFIG_SHELL} "${srctree}/scripts/file-size.sh" "$XIPIMAGE")
if [ "$file_end" != "$data_end" ]; then
	printf "end of xipImage doesn't match with _edata_loc (%#x vs %#x)\n" \
	       $(($file_end + $base_offset)) $_edata_loc 1>&2
	exit 1;
fi

# be ready to clean up
trap 'rm -f "$XIPIMAGE.tmp"; exit 1' 1 2 3

# substitute the data section by a compressed version
$DD if="$XIPIMAGE" count=$data_start iflag=count_bytes of="$XIPIMAGE.tmp"
$DD if="$XIPIMAGE"  skip=$data_start iflag=skip_bytes |
$_GZIP -9 >> "$XIPIMAGE.tmp"

# replace kernel binary
mv -f "$XIPIMAGE.tmp" "$XIPIMAGE"
