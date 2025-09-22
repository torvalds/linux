#	$OpenBSD: install.md,v 1.66 2025/03/29 14:26:41 kn Exp $
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
MDXAPERTURE=2
MDXDM=y
NCPU=$(sysctl -n hw.ncpufound)

# Two reasons to prefer GPT instead of MBR
grep -q -e '^efifb0 at mainbus0' -e '^acpi0 at bios0: ACPI [5-9]\.' \
    $DMESGBOOT && MDEFI=y

md_installboot() {
	if ! installboot -cr /mnt ${1}; then
		echo "\nFailed to install bootblocks."
		echo "You will not be able to boot OpenBSD from ${1}."
		exit
	fi
}

md_prep_fdisk() {
	local _disk=$1 _q _d

	while :; do
		_d=whole
		_q="Use (W)hole disk MBR, whole disk (G)PT"

		[[ $MDEFI == y ]] && _d=gpt

		if disk_has $_disk mbr || disk_has $_disk gpt; then
			fdisk $_disk
			if disk_has $_disk mbr openbsd ||
				disk_has $_disk gpt openbsd; then
				_q="$_q, (O)penBSD area"
				_d=OpenBSD
			fi
		else
			echo "No valid MBR or GPT."
		fi

		ask "$_q or (E)dit?" "$_d"
		case $resp in
		[wW]*)
			echo -n "Setting OpenBSD MBR partition to whole $_disk..."
			fdisk -iy $_disk >/dev/null
			echo "done."
			return ;;
		[gG]*)
			if [[ $MDEFI != y ]]; then
				ask_yn "An EFI/GPT disk may not boot. Proceed?" || continue
			fi

			echo -n "Setting OpenBSD GPT partition to whole $_disk..."
			fdisk -gy -b 532480 $_disk >/dev/null
			echo "done."
			return ;;
		[eE]*)
			if disk_has $_disk gpt; then
				# Manually configure the GPT.
				cat <<__EOT

You will now create two GPT partitions. The first must have an id
of 'EF' and be large enough to contain the OpenBSD boot programs,
at least 532480 blocks. The second must have an id of 'A6' and will
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

You will now create a single MBR partition to contain your OpenBSD data. This
partition must have an id of 'A6'; must *NOT* overlap other partitions; and
must be marked as the only active partition.  Inside the fdisk command, the
'manual' command describes all the fdisk commands in detail.

$(fdisk $_disk)
__EOT
				fdisk -e $_disk
				disk_has $_disk mbr openbsd && return
				echo -n "No OpenBSD partition in MBR,"
			fi
			echo " try again." ;;
		[oO]*)
			[[ $_d == OpenBSD ]] || continue
			# Is this a boot disk?
			if [[ $_disk == $ROOTDISK ]] &&
			    disk_has $_disk gpt && ! disk_has $_disk gpt efisys; then
				echo "No EFI Sys partition in GPT, try again."
				$AUTO && exit 1
				continue
			fi
			return ;;
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
	local _u _d=com

	for _u in $(scan_dmesg "/^$_d\([0-9]\) .*/s//\1/p"); do
		if [[ $_d$_u == $CONSOLE || -z $CONSOLE ]]; then
			CDEV=$_d$_u
			CPROM=com$_u
			CTTY=tty0$_u
			return
		fi
	done
}
