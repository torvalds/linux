#!/bin/bash
#
# Formats a floppy to use Syslinux

dummy=""


# need to have mtools installed
if [ -z `which mformat` -o -z `which mcopy` ]; then
	echo "You must have the mtools package installed to run this script"
	exit 1
fi


# need an arg for the location of the kernel
if [ -z "$1" ]; then
	echo "usage: `basename $0` path/to/linux/kernel"
	exit 1
fi


# need to have a root file system built
if [ ! -f rootfs.gz ]; then
	echo "You need to have a rootfs built first."
	echo "Hit RETURN to make one now or Control-C to quit."
	read dummy
	./mkrootfs.sh
fi


# prepare the floppy
echo "Please insert a blank floppy in the drive and press RETURN to format"
echo "(WARNING: All data will be erased! Hit Control-C to abort)"
read dummy

echo "Formatting the floppy..."
mformat a:
echo "Making it bootable with Syslinux..."
syslinux -s /dev/fd0
echo "Copying Syslinux configuration files..."
mcopy syslinux.cfg display.txt a:
echo "Copying root filesystem file..."
mcopy rootfs.gz a:
# XXX: maybe check for "no space on device" errors here
echo "Copying linux kernel..."
mcopy $1 a:linux
# XXX: maybe check for "no space on device" errors here too
echo "Finished: boot floppy created"
