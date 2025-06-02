#!/bin/bash
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2017 by Changbin Du <changbin.du@intel.com>
#
# Adapted from code in arch/x86/boot/Makefile by H. Peter Anvin and others
#
# "make fdimage/fdimage144/fdimage288/hdimage/isoimage"
# script for x86 architecture
#
# Arguments:
#   $1  - fdimage format
#   $2  - target image file
#   $3  - kernel bzImage file
#   $4  - mtools configuration file
#   $5  - kernel cmdline
#   $6+ - initrd image file(s)
#
# This script requires:
#   bash
#   syslinux
#   genisoimage
#   mtools (for fdimage* and hdimage)
#   edk2/OVMF (for hdimage)
#
# Otherwise try to stick to POSIX shell commands...
#

# Use "make V=1" to debug this script
case "${KBUILD_VERBOSE}" in
*1*)
        set -x
        ;;
esac

# Exit the top-level shell with an error
topshell=$$
trap 'exit 1' USR1
die() {
	echo ""        1>&2
	echo " *** $*" 1>&2
	echo ""        1>&2
	kill -USR1 $topshell
}

# Verify the existence and readability of a file
verify() {
	if [ ! -f "$1" -o ! -r "$1" ]; then
		die "Missing file: $1"
	fi
}

diskfmt="$1"
FIMAGE="$2"
FBZIMAGE="$3"
MTOOLSRC="$4"
KCMDLINE="$5"
shift 5				# Remaining arguments = initrd files

export MTOOLSRC

# common options for dd
dd='dd iflag=fullblock'

# Make sure the files actually exist
verify "$FBZIMAGE"

declare -a FDINITRDS
irdpfx=' initrd='
initrdopts_syslinux=''
initrdopts_efi=''
for f in "$@"; do
	if [ -f "$f" -a -r "$f" ]; then
	    FDINITRDS=("${FDINITRDS[@]}" "$f")
	    fname="$(basename "$f")"
	    initrdopts_syslinux="${initrdopts_syslinux}${irdpfx}${fname}"
	    irdpfx=,
	    initrdopts_efi="${initrdopts_efi} initrd=${fname}"
	fi
done

# Read a $3-byte littleendian unsigned value at offset $2 from file $1
le() {
	local n=0
	local m=1
	for b in $(od -A n -v -j $2 -N $3 -t u1 "$1"); do
		n=$((n + b*m))
		m=$((m * 256))
	done
	echo $n
}

# Get the EFI architecture name such that boot{name}.efi is the default
# boot file name. Returns false with no output if the file is not an
# EFI image or otherwise unknown.
efiarch() {
	[ -f "$1" ] || return
	[ $(le "$1" 0 2) -eq 23117 ] || return		# MZ magic
	peoffs=$(le "$1" 60 4)				# PE header offset
	[ $peoffs -ge 64 ] || return
	[ $(le "$1" $peoffs 4) -eq 17744 ] || return	# PE magic
	case $(le "$1" $((peoffs+4+20)) 2) in		# PE type
		267)	;;				# PE32
		523)	;;				# PE32+
		*) return 1 ;;				# Invalid
	esac
	[ $(le "$1" $((peoffs+4+20+68)) 2) -eq 10 ] || return # EFI app
	case $(le "$1" $((peoffs+4)) 2) in		# Machine type
		 332)	echo i386	;;
		 450)	echo arm	;;
		 512)	echo ia64	;;
		20530)	echo riscv32	;;
		20580)	echo riscv64	;;
		20776)	echo riscv128	;;
		34404)	echo x64	;;
		43620)	echo aa64	;;
	esac
}

# Get the combined sizes in bytes of the files given, counting sparse
# files as full length, and padding each file to cluster size
cluster=16384
filesizes() {
	local t=0
	local s
	for s in $(ls -lnL "$@" 2>/dev/null | awk '/^-/{ print $5; }'); do
		t=$((t + ((s+cluster-1)/cluster)*cluster))
	done
	echo $t
}

# Expand directory names which should be in /usr/share into a list
# of possible alternatives
sharedirs() {
	local dir file
	for dir in /usr/share /usr/lib64 /usr/lib; do
		for file; do
			echo "$dir/$file"
			echo "$dir/${file^^}"
		done
	done
}
efidirs() {
	local dir file
	for dir in /usr/share /boot /usr/lib64 /usr/lib; do
		for file; do
			echo "$dir/$file"
			echo "$dir/${file^^}"
		done
	done
}

findsyslinux() {
	local f="$(find -L $(sharedirs syslinux isolinux) \
		    -name "$1" -readable -type f -print -quit 2>/dev/null)"
	if [ ! -f "$f" ]; then
		die "Need a $1 file, please install syslinux/isolinux."
	fi
	echo "$f"
	return 0
}

findovmf() {
	local arch="$1"
	shift
	local -a names=(-false)
	local name f
	for name; do
		names=("${names[@]}" -or -iname "$name")
	done
	for f in $(find -L $(efidirs edk2 ovmf) \
			\( "${names[@]}" \) -readable -type f \
			-print 2>/dev/null); do
		if [ "$(efiarch "$f")" = "$arch" ]; then
			echo "$f"
			return 0
		fi
	done
	die "Need a $1 file for $arch, please install EDK2/OVMF."
}

do_mcopy() {
	if [ ${#FDINITRDS[@]} -gt 0 ]; then
		mcopy "${FDINITRDS[@]}" "$1"
	fi
	if [ -n "$efishell" ]; then
		mmd "$1"EFI "$1"EFI/Boot
		mcopy "$efishell" "$1"EFI/Boot/boot${kefiarch}.efi
	fi
	if [ -n "$kefiarch" ]; then
		echo linux "$KCMDLINE$initrdopts_efi" | \
			mcopy - "$1"startup.nsh
	fi
	echo default linux "$KCMDLINE$initrdopts_syslinux" | \
		mcopy - "$1"syslinux.cfg
	mcopy "$FBZIMAGE" "$1"linux
}

genbzdisk() {
	verify "$MTOOLSRC"
	mformat -v 'LINUX_BOOT' a:
	syslinux "$FIMAGE"
	do_mcopy a:
}

genfdimage144() {
	verify "$MTOOLSRC"
	$dd if=/dev/zero of="$FIMAGE" bs=1024 count=1440 2>/dev/null
	mformat -v 'LINUX_BOOT' v:
	syslinux "$FIMAGE"
	do_mcopy v:
}

genfdimage288() {
	verify "$MTOOLSRC"
	$dd if=/dev/zero of="$FIMAGE" bs=1024 count=2880 2>/dev/null
	mformat -v 'LINUX_BOOT' w:
	syslinux "$FIMAGE"
	do_mcopy w:
}

genhdimage() {
	verify "$MTOOLSRC"
	mbr="$(findsyslinux mbr.bin)"
	kefiarch="$(efiarch "$FBZIMAGE")"
	if [ -n "$kefiarch" ]; then
		# The efishell provides command line handling
		efishell="$(findovmf $kefiarch shell.efi shell${kefiarch}.efi)"
		ptype='-T 0xef'	# EFI system partition, no GPT
	fi
	sizes=$(filesizes "$FBZIMAGE" "${FDINITRDS[@]}" "$efishell")
	# Allow 1% + 2 MiB for filesystem and partition table overhead,
	# syslinux, and config files; this is probably excessive...
	megs=$(((sizes + sizes/100 + 2*1024*1024 - 1)/(1024*1024)))
	$dd if=/dev/zero of="$FIMAGE" bs=$((1024*1024)) count=$megs 2>/dev/null
	mpartition -I -c -s 32 -h 64 $ptype -b 64 -a p:
	$dd if="$mbr" of="$FIMAGE" bs=440 count=1 conv=notrunc 2>/dev/null
	mformat -v 'LINUX_BOOT' -s 32 -h 64 -c $((cluster/512)) -t $megs h:
	syslinux --offset $((64*512)) "$FIMAGE"
	do_mcopy h:
}

geniso() {
	tmp_dir="$(dirname "$FIMAGE")/isoimage"
	rm -rf "$tmp_dir"
	mkdir "$tmp_dir"
	isolinux=$(findsyslinux isolinux.bin)
	ldlinux=$(findsyslinux  ldlinux.c32)
	cp "$isolinux" "$ldlinux" "$tmp_dir"
	cp "$FBZIMAGE" "$tmp_dir"/linux
	echo default linux "$KCMDLINE" > "$tmp_dir"/isolinux.cfg
	if [ ${#FDINITRDS[@]} -gt 0 ]; then
		cp "${FDINITRDS[@]}" "$tmp_dir"/
	fi
	genisoimage -J -r -appid 'LINUX_BOOT' -input-charset=utf-8 \
		    -quiet -o "$FIMAGE" -b isolinux.bin \
		    -c boot.cat -no-emul-boot -boot-load-size 4 \
		    -boot-info-table "$tmp_dir"
	isohybrid "$FIMAGE" 2>/dev/null || true
	rm -rf "$tmp_dir"
}

rm -f "$FIMAGE"

case "$diskfmt" in
	bzdisk)     genbzdisk;;
	fdimage144) genfdimage144;;
	fdimage288) genfdimage288;;
	hdimage)    genhdimage;;
	isoimage)   geniso;;
	*)          die "Unknown image format: $diskfmt";;
esac
