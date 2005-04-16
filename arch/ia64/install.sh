#!/bin/sh
#
# arch/ia64/install.sh
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995 by Linus Torvalds
#
# Adapted from code in arch/i386/boot/Makefile by H. Peter Anvin
#
# "make install" script for ia64 architecture
#
# Arguments:
#   $1 - kernel version
#   $2 - kernel image file
#   $3 - kernel map file
#   $4 - default install path (blank if root directory)
#

# User may have a custom install script

if [ -x ~/bin/installkernel ]; then exec ~/bin/installkernel "$@"; fi
if [ -x /sbin/installkernel ]; then exec /sbin/installkernel "$@"; fi

# Default install - same as make zlilo

if [ -f $4/vmlinuz ]; then
	mv $4/vmlinuz $4/vmlinuz.old
fi

if [ -f $4/System.map ]; then
	mv $4/System.map $4/System.old
fi

cat $2 > $4/vmlinuz
cp $3 $4/System.map

test -x /usr/sbin/elilo && /usr/sbin/elilo
