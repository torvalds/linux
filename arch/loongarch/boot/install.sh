#!/bin/sh
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995 by Linus Torvalds
#
# Adapted from code in arch/i386/boot/Makefile by H. Peter Anvin
# Adapted from code in arch/i386/boot/install.sh by Russell King
#
# "make install" script for the LoongArch Linux port
#
# Arguments:
#   $1 - kernel version
#   $2 - kernel image file
#   $3 - kernel map file
#   $4 - default install path (blank if root directory)

set -e

case "${2##*/}" in
vmlinux.elf)
  echo "Installing uncompressed vmlinux.elf kernel"
  base=vmlinux
  ;;
vmlinux.efi)
  echo "Installing uncompressed vmlinux.efi kernel"
  base=vmlinux
  ;;
vmlinuz.efi)
  echo "Installing gzip/zstd compressed vmlinuz.efi kernel"
  base=vmlinuz
  ;;
*)
 echo "Warning: Unexpected kernel type"
 exit 1
 ;;
esac

if [ -f $4/$base-$1 ]; then
  mv $4/$base-$1 $4/$base-$1.old
fi
cat $2 > $4/$base-$1

# Install system map file
if [ -f $4/System.map-$1 ]; then
  mv $4/System.map-$1 $4/System.map-$1.old
fi
cp $3 $4/System.map-$1

# Install kernel config file
if [ -f $4/config-$1 ]; then
  mv $4/config-$1 $4/config-$1.old
fi
cp .config $4/config-$1
