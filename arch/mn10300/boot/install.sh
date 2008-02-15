#!/bin/sh
#
# arch/mn10300/boot/install -c.sh
#
# This file is subject to the terms and conditions of the GNU General Public
# Licence.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995 by Linus Torvalds
#
# Adapted from code in arch/i386/boot/Makefile by H. Peter Anvin
#
# "make install -c" script for i386 architecture
#
# Arguments:
#   $1 - kernel version
#   $2 - kernel image file
#   $3 - kernel map file
#   $4 - default install -c path (blank if root directory)
#   $5 - boot rom file
#

# User may have a custom install -c script

rm -fr $4/../usr/include/linux $4/../usr/include/asm
install -c -m 0755 $2 $4/vmlinuz
install -c -m 0755 $5 $4/boot.rom
install -c -m 0755 -d $4/../usr/include/linux
cd $TOPDIR/include/linux
for i in `find . -maxdepth 1 -name '*.h' -print`; do
  install -c -m 0644 $i $4/../usr/include/linux
done
install -c -m 0755 -d $4/../usr/include/linux/byteorder
cd $TOPDIR/include/linux/byteorder
for i in `find . -name '*.h' -print`; do
  install -c -m 0644 $i $4/../usr/include/linux/byteorder
done
install -c -m 0755 -d $4/../usr/include/linux/lockd
cd $TOPDIR/include/linux/lockd
for i in `find . -name '*.h' -print`; do
  install -c -m 0644 $i $4/../usr/include/linux/lockd
done
install -c -m 0755 -d $4/../usr/include/linux/netfilter_ipv4
cd $TOPDIR/include/linux/netfilter_ipv4
for i in `find . -name '*.h' -print`; do
  install -c -m 0644 $i $4/../usr/include/linux/netfilter_ipv4
done
install -c -m 0755 -d $4/../usr/include/linux/nfsd
cd $TOPDIR/include/linux/nfsd
for i in `find . -name '*.h' -print`; do
  install -c -m 0644 $i $4/../usr/include/linux/nfsd/$i
done
install -c -m 0755 -d $4/../usr/include/linux/raid
cd $TOPDIR/include/linux/raid
for i in `find . -name '*.h' -print`; do
  install -c -m 0644 $i $4/../usr/include/linux/raid
done
install -c -m 0755 -d $4/../usr/include/linux/sunrpc
cd $TOPDIR/include/linux/sunrpc
for i in `find . -name '*.h' -print`; do
  install -c -m 0644 $i $4/../usr/include/linux/sunrpc
done
install -c -m 0755 -d $4/../usr/include/asm
cd $TOPDIR/include/asm
for i in `find . -name '*.h' -print`; do
  install -c -m 0644 $i $4/../usr/include/asm
done
