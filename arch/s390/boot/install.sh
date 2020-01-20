#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# arch/s390x/boot/install.sh
#
# Copyright (C) 1995 by Linus Torvalds
#
# Adapted from code in arch/i386/boot/Makefile by H. Peter Anvin
#
# "make install" script for s390 architecture
#
# Arguments:
#   $1 - kernel version
#   $2 - kernel image file
#   $3 - kernel map file
#   $4 - default install path (blank if root directory)
#

# User may have a custom install script

if [ -x ~/bin/${INSTALLKERNEL} ]; then exec ~/bin/${INSTALLKERNEL} "$@"; fi
if [ -x /sbin/${INSTALLKERNEL} ]; then exec /sbin/${INSTALLKERNEL} "$@"; fi

echo "Warning: '${INSTALLKERNEL}' command not available - additional " \
     "bootloader config required" >&2
if [ -f $4/vmlinuz-$1 ]; then mv $4/vmlinuz-$1 $4/vmlinuz-$1.old; fi
if [ -f $4/System.map-$1 ]; then mv $4/System.map-$1 $4/System.map-$1.old; fi

cat $2 > $4/vmlinuz-$1
cp $3 $4/System.map-$1
