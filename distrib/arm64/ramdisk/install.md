#	$OpenBSD: install.md,v 1.52 2025/09/01 18:56:04 mglocker Exp $
#
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

MDBOOTSR=y
NCPU=$(sysctl -n hw.ncpufound)
COMPATIBLE=$(sysctl -n machdep.compatible)
MOUNT_ARGS_msdos="-o-l"
KEEP_EFI_SYS=false

md_installboot() {
	local _disk=$1 _reason=$2 _rerun=false _chunks _bootdisk _mdec _plat

	case ${COMPATIBLE} in
	apple,*)		_plat=apple
				[[ $_reason == apple-boot ]] && _rerun=true;;
	raspberrypi,*)		_plat=rpi;;
	esac

	if ! installboot -cr /mnt $_disk; then
		echo "\nFailed to install bootblocks."
		echo "You will not be able to boot OpenBSD from $_disk."
		exit
	fi

	$_rerun && return

	# Apply some final tweaks on selected platforms
	_mdec=/usr/mdec/$_plat

	case $_plat in
	apple)
		(cd /etc/firmware; for _dir in apple{,-bwfm}; do
			[[ -d $_dir ]] && pax -rw $_dir /mnt/etc/firmware
		done)
		;;
	rpi)
		_chunks=$(get_softraid_chunks)
		for _bootdisk in ${_chunks:-$_disk}; do
			mount ${MOUNT_ARGS_msdos} /dev/${_bootdisk}i /mnt/mnt
			cp $_mdec/{bootcode.bin,start*.elf,fixup*.dat,*.dtb} /mnt/mnt/
			cp $_mdec/u-boot.bin /mnt/mnt/
			mkdir -p /mnt/mnt/overlays
			cp $_mdec/bcm2712d0.dtbo /mnt/mnt/overlays
			cp $_mdec/disable-bt.dtbo /mnt/mnt/overlays
			if [[ ! -f /mnt/mnt/config.txt ]]; then
				cat > /mnt/mnt/config.txt<<-__EOT
					arm_64bit=1
					enable_uart=1
					dtoverlay=disable-bt
					kernel=u-boot.bin
				__EOT
			fi
		done
		umount /mnt/mnt
		;;
	esac
}

md_fw() {
	md_installboot "$@"
}

md_prep_fdisk() {
	local _disk=$1 _d _type=MBR

	local bootparttype="C"
	local bootsectorstart="32768"
	local bootsectorsize="32768"
	local bootfstype="msdos"

	case ${COMPATIBLE} in
	openbsd,acpi)		bootsectorsize=532480;;
	esac

	while :; do
		_d=whole
		if disk_has $_disk gpt; then
			_type=GPT
			fdisk $_disk
		elif disk_has $_disk mbr; then
			fdisk $_disk
		else
			echo "MBR has invalid signature; not showing it."
		fi
		ask "Use (W)hole disk or (E)dit the ${_type}?" $_d
		case $resp in
		[wW]*)
			echo -n "Creating a ${bootfstype} partition and an OpenBSD partition for rest of $_disk..."
			if disk_has $_disk gpt apfsisc; then
				# On Apple hardware, the existing EFI Sys
				# partition contains boot firmware and MUST NOT
				# be recreated.
				KEEP_EFI_SYS=true

				# Is this a boot disk?
				if [[ $_disk == $ROOTDISK ]]; then
					fdisk -Ay -b "${bootsectorsize}" ${_disk} >/dev/null
				else
					fdisk -Ay ${_disk} >/dev/null
				fi
			elif disk_has $_disk gpt; then
				# Is this a boot disk?
				if [[ $_disk == $ROOTDISK ]]; then
					fdisk -gy -b "${bootsectorsize}" ${_disk} >/dev/null

					# With root on softraid,
					# 'installboot -p' on the root disk
					# nukes the EFI Sys partition on
					# the chunks.
					$KEEP_EFI_SYS || installboot -p $_disk
				else
					fdisk -gy ${_disk} >/dev/null
				fi
			else
				fdisk -iy -b "${bootsectorsize}@${bootsectorstart}:${bootparttype}" ${_disk} >/dev/null

				# With root on softraid, 'installboot -p' on
				# the root disk nukes the EFI Sys partition on
				# the chunks.
				$KEEP_EFI_SYS || installboot -p $_disk
			fi
			echo "done."
			return ;;
		[eE]*)
			if disk_has $_disk gpt; then
				# Manually configure the GPT.
				cat <<__EOT

You will now create two GPT partitions. The first must have an id
of 'EF' and be large enough to contain the OpenBSD boot programs,
at least 32768 blocks. The second must have an id of 'A6' and will
contain your OpenBSD data. Neither may overlap other partitions.
Inside the fdisk command, the 'manual' command describes the fdisk
commands in detail.

$(fdisk $_disk)
__EOT
				fdisk -e $_disk

				if ! disk_has $_disk gpt openbsd; then
					echo -n "No OpenBSD partition in GPT,"
				elif ! disk_has $_disk gpt efisys; then
					echo -n "No EFI Sys partition in GPT,"
				else
					return
				fi
			else
				# Manually configure the MBR.
				cat <<__EOT

You will now create one MBR partition to contain your OpenBSD data
and one MBR partition on which the OpenBSD boot program is located.
Neither partition will overlap any other partition.

The OpenBSD MBR partition will have an id of 'A6' and the boot MBR
partition will have an id of '${bootparttype}' (${bootfstype}).
The boot partition will be at least 16MB and be the first 'MSDOS'
partition on the disk.

$(fdisk ${_disk})
__EOT
				fdisk -e ${_disk}
				disk_has $_disk mbr openbsd && return
				echo -n "No OpenBSD partition in MBR,"
			fi
			echo " try again." ;;
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
}

md_consoleinfo() {
	local _fw _fw2

	DEFCONS=y
	case $(scan_dmesg '/^\([^ ]*\).*: console.*std.*$/s//\1/p') in
	wsdisplay0)
		CTTY=ttyC0;;
	*)
		CTTY=console;;
	esac
	case $CSPEED in
	9600|19200|38400|57600|115200|1500000)
		;;
	*)
		CSPEED=115200;;
	esac

	case ${COMPATIBLE} in
	apple,*)
		_fw=$(dmesgtail | sed -n '\!^bwfm0: failed!{s!^.*/\(.*\),.*$!\1!p;q;}')
		_fw2=${COMPATIBLE##*apple,}
		make_dev sd0
		if mount -o ro /dev/sd0l /mnt2 2>/dev/null; then
			rm -rf /usr/mdec/rpi /etc/firmware/apple
			rm -rf /etc/firmware/brcm /etc/firmware/apple-bwfm
			if [[ -s /mnt2/vendorfw/firmware.tar ]]; then
				[[ -n $_fw ]] && tar -x -C /etc/firmware \
				    -f /mnt2/vendorfw/firmware.tar "*$_fw*" 2>/dev/null
				[[ -n $_fw2 ]] && tar -x -C /etc/firmware \
				    -f /mnt2/vendorfw/firmware.tar "*$_fw2*" 2>/dev/null
				mv /etc/firmware/brcm /etc/firmware/apple-bwfm 2>/dev/null
			fi
			umount /mnt2
		fi
	esac
}
