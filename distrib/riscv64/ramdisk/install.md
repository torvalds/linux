#	$OpenBSD: install.md,v 1.10 2023/10/11 17:53:52 kn Exp $
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

md_installboot() {
	if ! installboot -r /mnt ${1}; then
		echo "\nFailed to install bootblocks."
		echo "You will not be able to boot OpenBSD from ${1}."
		exit
	fi
}

md_prep_fdisk() {
	local _disk=$1 _d _type=MBR

	local bootpart=
	local bootparttype="C"
	local bootsectorstart="32768"
	local bootsectorsize="32768"
	local bootfstype="msdos"

	while :; do
		_d=whole
		if disk_has $_disk gpt; then
			# Is this a boot disk?
			[[ $_disk == $ROOTDISK ]] && bootpart="-b ${bootsectorsize}"
			_type=GPT
			fdisk $_disk
		elif disk_has $_disk mbr; then
			fdisk $_disk
		else
			echo "MBR has invalid signature; not showing it."
		fi
		ask "Use (W)hole disk or (E)dit the ${_type}?" "$_d"
		case $resp in
		[wW]*)
			echo -n "Creating a ${bootfstype} partition and an OpenBSD partition for rest of $_disk..."
			if disk_has $_disk gpt biosboot; then
				# Preserve BIOS boot partition as it might
				# contain a PolarFire SoC HSS payload.
				fdisk -Ay ${bootpart} ${_disk} >/dev/null
			elif disk_has $_disk gpt; then
				fdisk -gy ${bootpart} ${_disk} >/dev/null
			else
				fdisk -iy -b "${bootsectorsize}@${bootsectorstart}:${bootparttype}" ${_disk} >/dev/null
			fi
			echo "done."
			installboot -p $_disk
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
	CTTY=console
	DEFCONS=y
	case $CSPEED in
	9600|19200|38400|57600|115200|1500000)
		;;
	*)
		CSPEED=115200;;
	esac
}
