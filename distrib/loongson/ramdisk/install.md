#	$OpenBSD: install.md,v 1.32 2023/03/07 17:37:26 kn Exp $
#
# Copyright (c) 1996 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Jason R. Thorpe.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
#
# machine dependent section of installation/upgrade script.
#

md_installboot() {
	local _disk=$1
	case $(sysctl -n hw.product) in
	Gdium)
		mount -t ext2fs /dev/${_disk}i /mnt2
		mkdir -p /mnt2/boot
		cp /mnt/usr/mdec/boot /mnt2/boot/boot
		cp /mnt/bsd /mnt2/boot/bsd
		umount /mnt2
		;;
	*)
		if ! installboot -r /mnt ${_disk}; then
			echo "\nFailed to install bootblocks."
			echo "You will not be able to boot OpenBSD from ${_disk}."
			exit
		fi
		;;
	esac
}

md_prep_fdisk() {
	local _disk=$1 _q _d _o

	while :; do
		_d=whole
		if disk_has $_disk mbr; then
			fdisk $_disk
			if disk_has $_disk mbr openbsd; then
				_q=", use the (O)penBSD area"
				_d=OpenBSD
			fi
		else
			echo "MBR has invalid signature; not showing it."
		fi
		ask "Use (W)hole disk$_q or (E)dit the MBR?" "$_d"
		case $resp in
		[wW]*)
			case $(sysctl -n hw.product) in
			Gdium)
				echo -n "Creating a 32MB ext2 partition and an OpenBSD partition for rest of $_disk..."
				fdisk -iy -b "65536@1:83" $_disk >/dev/null
				_o="-O 1 -b 4096"
				;;
			EBT700)
				echo -n "Creating a 1MB ext2 partition and an OpenBSD partition for rest of $_disk..."
				fdisk -iy -b "2048@1:83" $_disk >/dev/null
				_o="-O 1"
				;;
			*)
				echo -n "Creating a 1MB ext2 partition and an OpenBSD partition for rest of $_disk..."
				fdisk -iy -b "2048@1:83" $_disk >/dev/null
				_o=""
				;;
			esac
			echo "done."
			installboot -p $_disk
			break ;;
		[eE]*)
			# Manually configure the MBR.
			cat <<__EOT

You will now create one MBR partition to contain your OpenBSD data
and one MBR partition to contain the program that PMON uses
to boot OpenBSD. Neither partition will overlap any other partition.

The OpenBSD MBR partition will have an id of 'A6' and the boot MBR
partition will have an id of '83' (Linux files). The boot partition will be
at least 1MB and be the first 'Linux files' partition on the disk.
The installer assumes there is already an ext2 or ext3 filesystem on the
first 'Linux files' partition.

$(fdisk ${_disk})
__EOT
			fdisk -e $_disk
			disk_has $_disk mbr linux ||
				{ echo "\nNo Linux files (id 83) partition!\n"; continue; }
			disk_has $_disk mbr openbsd ||
				{ echo "\nNo OpenBSD (id A6) partition!\n"; continue; }
			disklabel $_disk 2>/dev/null | grep -q "^  i:" || disklabel -w -d $_disk
			break ;;
		[oO]*)
			[[ $_d == OpenBSD ]] || continue
			break ;;
		esac
	done

}

md_prep_disklabel() {
	local _disk=$1 _f=/tmp/i/fstab.$1

	md_prep_fdisk $_disk

	disklabel_autolayout $_disk $_f || return
	[[ -s $_f ]] && return

	# Edit disklabel manually.
	# Abandon all hope, ye who enter here.
	disklabel -F $_f -E $_disk
}

md_congrats() {
	cat <<__EOT

Once the machine has rebooted use PMON to boot into OpenBSD, as
described in the INSTALL.$ARCH document.
To load the OpenBSD bootloader, use 'boot /dev/fs/ext2@wd0/boot/boot',
where wd0 is the PMON name of the boot disk.

__EOT
}

md_consoleinfo() {
}
