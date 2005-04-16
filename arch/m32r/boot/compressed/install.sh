#!/bin/sh
#
# arch/sh/boot/install.sh
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995 by Linus Torvalds
#
# Adapted from code in arch/i386/boot/Makefile by H. Peter Anvin
# Adapted from code in arch/i386/boot/install.sh by Russell King
# Adapted from code in arch/arm/boot/install.sh by Stuart Menefy
# Adapted from code in arch/sh/boot/install.sh by Takeo Takahashi
#
# "make install" script for sh architecture
#
# Arguments:
#   $1 - kernel version
#   $2 - kernel image file
#   $3 - kernel map file
#   $4 - default install path (blank if root directory)
#

# User may have a custom install script

if [ -x /sbin/installkernel ]; then
  exec /sbin/installkernel "$@"
fi

if [ "$2" = "zImage" ]; then
# Compressed install
  echo "Installing compressed kernel"
  if [ -f $4/vmlinuz-$1 ]; then
    mv $4/vmlinuz-$1 $4/vmlinuz.old
  fi

  if [ -f $4/System.map-$1 ]; then
    mv $4/System.map-$1 $4/System.old
  fi

  cat $2 > $4/vmlinuz-$1
  cp $3 $4/System.map-$1
else
# Normal install
  echo "Installing normal kernel"
  if [ -f $4/vmlinux-$1 ]; then
    mv $4/vmlinux-$1 $4/vmlinux.old
  fi

  if [ -f $4/System.map ]; then
    mv $4/System.map $4/System.old
  fi

  cat $2 > $4/vmlinux-$1
  cp $3 $4/System.map
fi
