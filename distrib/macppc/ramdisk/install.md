#	$OpenBSD: install.md,v 1.78 2023/05/26 11:41:50 kn Exp $
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

MDXAPERTURE=2
MDXDM=y
NCPU=$(sysctl -n hw.ncpufound)

md_installboot() {
	if ! installboot -r /mnt ${1}; then
		echo "\nFailed to install bootblocks."
		echo "You will not be able to boot OpenBSD from ${1}."
		exit
	fi
}

md_prep_MBR() {
	local _disk=$1 _q _d

	if disk_has $_disk hfs; then
		cat <<__EOT

WARNING: Putting an MBR partition table on $_disk will DESTROY the existing HFS
         partitions and HFS partition table:
$(pdisk -l $_disk)

__EOT
		ask_yn "Are you *sure* you want an MBR partition table on $_disk?" || return 1
	fi

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
			echo -n "Creating a 1MB DOS partition and an OpenBSD partition for rest of $_disk..."
			dd if=/dev/zero of=/dev/r${_disk}c bs=1m count=1 status=none
			fdisk -iy -b "2048@1:06" $_disk >/dev/null
			echo "done."
			break ;;
		[eE]*)
			# Manually configure the MBR.
			cat <<__EOT

You will now create one MBR partition to contain your OpenBSD data
and one MBR partition to contain the program that Open Firmware uses
to boot OpenBSD. Neither partition will overlap any other partition.

The OpenBSD MBR partition will have an id of 'A6' and the boot MBR
partition will have an id of '06' (DOS). The boot partition will be
at least 1MB and be marked as the *only* active partition.

$(fdisk $_disk)
__EOT
			fdisk -e $_disk
			disk_has $_disk mbr dos ||
				{ echo "\nNo DOS (id 06) partition!\n"; continue; }
			disk_has $_disk mbr dos_active ||
				{ echo "\nNo active DOS partition!\n"; continue; }
			disk_has $_disk mbr openbsd ||
				{ echo "\nNo OpenBSD (id A6) partition!\n"; continue; }
			break ;;
		[oO]*)
			[[ $_d == OpenBSD ]] || continue
			break ;;
		esac
	done

	installboot -p $_disk
}

md_prep_HFS() {
	local _disk=$1 _d _q

	while :; do
		_q=
		_d=Modify
		disk_has $_disk hfs openbsd &&
			{ _q="Use the (O)penBSD partition, "; _d=OpenBSD; }
		pdisk -l $_disk
		ask "$_q(M)odify a partition or (A)bort?" "$_d"
		case $resp in
		[aA]*)	return 1 ;;
		[oO]*)	return 0 ;;
		[mM]*)	pdisk $_disk
			disk_has $_disk hfs openbsd && break
			echo "\nNo 'OpenBSD'-type partition named 'OpenBSD'!"
		esac
	done

	return 0;
}

md_prep_disklabel() {
	local _disk=$1 _f=/tmp/i/fstab.$1

	PARTTABLE=
	while [[ -z $PARTTABLE ]]; do
		resp=MBR
		disk_has $_disk hfs && ask "Use (H)FS or (M)BR partition table?" MBR
		case $resp in
		[mM]*)	md_prep_MBR $_disk && PARTTABLE=MBR ;;
		[hH]*)	md_prep_HFS $_disk && PARTTABLE=HFS ;;
		esac
	done

	disklabel_autolayout $_disk $_f || return
	[[ -s $_f ]] && return

	# Edit disklabel manually.
	# Abandon all hope, ye who enter here.
	disklabel -F $_f -E $_disk
}

md_congrats() {
	cat <<__EOT

INSTALL.$ARCH describes how to configure Open Firmware to boot OpenBSD. The
command to boot OpenBSD will be something like 'boot hd:,ofwboot /bsd'.
__EOT
	if [[ $PARTTABLE == HFS ]]; then
		cat <<__EOT

NOTE: You must use MacOS to copy 'ofwboot' from the OpenBSD install media to
the first HFS partition of $ROOTDISK.
__EOT
	fi

}

md_consoleinfo() {
	local _u _d=zstty

	for _u in $(scan_dmesg "/^$_d\([0-9]\) .*/s//\1/p"); do
		if [[ $_d$_u == $CONSOLE || -z $CONSOLE ]]; then
			CDEV=$_d$_u
			: ${CSPEED:=57600}
			set -- a b c d e f g h i j
			shift $_u
			CTTY=tty$1
			return
		fi
	done
}
