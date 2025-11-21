#!/bin/sh
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995 by Linus Torvalds
#
# Blatantly stolen from in arch/i386/boot/install.sh by Dave Hansen 
#
# "make install" script for ppc64 architecture
#
# Arguments:
#   $1 - kernel version
#   $2 - kernel image file
#   $3 - kernel map file
#   $4 - default install path (blank if root directory)

set -e

# this should work for both the pSeries zImage and the iSeries vmlinux.sm
image_name=$(basename "$2")


echo "Warning: '${INSTALLKERNEL}' command not available... Copying" \
     "directly to $4/$image_name-$1" >&2

if [ -f "$4"/"$image_name"-"$1" ]; then
	mv "$4"/"$image_name"-"$1" "$4"/"$image_name"-"$1".old
fi

if [ -f "$4"/System.map-"$1" ]; then
	mv "$4"/System.map-"$1" "$4"/System-"$1".old
fi

cat "$2" > "$4"/"$image_name"-"$1"
cp "$3" "$4"/System.map-"$1"
