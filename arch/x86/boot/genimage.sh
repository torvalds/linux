#!/bin/sh
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2017 by Changbin Du <changbin.du@intel.com>
#
# Adapted from code in arch/x86/boot/Makefile by H. Peter Anvin and others
#
# "make fdimage/fdimage144/fdimage288/isoimage" script for x86 architecture
#
# Arguments:
#   $1 - fdimage format
#   $2 - target image file
#   $3 - kernel bzImage file
#   $4 - mtool configuration file
#   $5 - kernel cmdline
#   $6 - inird image file
#

verify () {
	if [ ! -f "$1" ]; then
		echo ""                                                   1>&2
		echo " *** Missing file: $1"                              1>&2
		echo ""                                                   1>&2
		exit 1
	fi
}


export MTOOLSRC=$4
FIMAGE=$2
FBZIMAGE=$3
KCMDLINE=$5
FDINITRD=$6

# Make sure the files actually exist
verify "$FBZIMAGE"
verify "$MTOOLSRC"

genbzdisk() {
	mformat a:
	syslinux $FIMAGE
	echo "$KCMDLINE" | mcopy - a:syslinux.cfg
	if [ -f "$FDINITRD" ] ; then
		mcopy "$FDINITRD" a:initrd.img
	fi
	mcopy $FBZIMAGE a:linux
}

genfdimage144() {
	dd if=/dev/zero of=$FIMAGE bs=1024 count=1440
	mformat v:
	syslinux $FIMAGE
	echo "$KCMDLINE" | mcopy - v:syslinux.cfg
	if [ -f "$FDINITRD" ] ; then
		mcopy "$FDINITRD" v:initrd.img
	fi
	mcopy $FBZIMAGE v:linux
}

genfdimage288() {
	dd if=/dev/zero of=$FIMAGE bs=1024 count=2880
	mformat w:
	syslinux $FIMAGE
	echo "$KCMDLINE" | mcopy - W:syslinux.cfg
	if [ -f "$FDINITRD" ] ; then
		mcopy "$FDINITRD" w:initrd.img
	fi
	mcopy $FBZIMAGE w:linux
}

genisoimage() {
	tmp_dir=`dirname $FIMAGE`/isoimage
	rm -rf $tmp_dir
	mkdir $tmp_dir
	for i in lib lib64 share end ; do
		if [ -f /usr/$i/syslinux/isolinux.bin ] ; then
			cp /usr/$i/syslinux/isolinux.bin $tmp_dir
			if [ -f /usr/$i/syslinux/ldlinux.c32 ]; then
				cp /usr/$i/syslinux/ldlinux.c32 $tmp_dir
			fi
			break
		fi
		if [ $i = end ] ; then exit 1 ; fi ;
	done
	cp $FBZIMAGE $tmp_dir/linux
	echo "$KCMDLINE" > $tmp_dir/isolinux.cfg
	if [ -f "$FDINITRD" ] ; then
		cp "$FDINITRD" $tmp_dir/initrd.img
	fi
	mkisofs -J -r -o $FIMAGE -b isolinux.bin -c boot.cat \
		-no-emul-boot -boot-load-size 4 -boot-info-table $tmp_dir
	isohybrid $FIMAGE 2>/dev/null || true
	rm -rf $tmp_dir
}

case $1 in
	bzdisk)     genbzdisk;;
	fdimage144) genfdimage144;;
	fdimage288) genfdimage288;;
	isoimage)   genisoimage;;
	*)          echo 'Unknown image format'; exit 1;
esac
