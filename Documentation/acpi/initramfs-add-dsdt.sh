#!/bin/bash
# Adds a DSDT file to the initrd (if it's an initramfs)
# first argument is the name of archive
# second argument is the name of the file to add
# The file will be copied as /DSDT.aml

# 20060126: fix "Premature end of file" with some old cpio (Roland Robic)
# 20060205: this time it should really work

# check the arguments
if [ $# -ne 2 ]; then
	program_name=$(basename $0)
	echo "\
$program_name: too few arguments
Usage: $program_name initrd-name.img DSDT-to-add.aml
Adds a DSDT file to an initrd (in initramfs format)

  initrd-name.img: filename of the initrd in initramfs format
  DSDT-to-add.aml: filename of the DSDT file to add
  " 1>&2
    exit 1
fi

# we should check it's an initramfs

tempcpio=$(mktemp -d)
# cleanup on exit, hangup, interrupt, quit, termination
trap 'rm -rf $tempcpio' 0 1 2 3 15

# extract the archive
gunzip -c "$1" > "$tempcpio"/initramfs.cpio || exit 1

# copy the DSDT file at the root of the directory so that we can call it "/DSDT.aml"
cp -f "$2" "$tempcpio"/DSDT.aml

# add the file
cd "$tempcpio"
(echo DSDT.aml | cpio --quiet -H newc -o -A -O "$tempcpio"/initramfs.cpio) || exit 1
cd "$OLDPWD"

# re-compress the archive
gzip -c "$tempcpio"/initramfs.cpio > "$1"

