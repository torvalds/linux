#	$OpenBSD: install.md,v 1.33 2017/07/28 18:15:44 rpe Exp $
#
# machine dependent section of installation/upgrade script.
#

MDTERM=vt100
NCPU=$(sysctl -n hw.ncpufound)

md_installboot() {
	if ! installboot -r /mnt ${1}; then
		echo "\nFailed to install bootblocks."
		echo "You will not be able to boot OpenBSD from ${1}."
		exit
	fi
}

md_prep_disklabel() {
	local _disk=$1 _f=/tmp/i/fstab.$1

	installboot $_disk

	disklabel_autolayout $_disk $_f || return
	[[ -s $_f ]] && return

	# Edit disklabel manually.
	# Abandon all hope, ye who enter here.
	disklabel -F $_f -E $_disk
}

md_congrats() {
}

md_consoleinfo() {
}
