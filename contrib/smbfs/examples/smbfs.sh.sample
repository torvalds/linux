#!/bin/sh
#
# $Id: smbfs.sh.sample,v 1.3 2001/01/13 04:50:36 bp Exp $
#
# Location: /usr/local/etc/rc.d/smbfs.sh
#
# Simple script to mount smbfs file systems at startup.
# It assumes that all mount points described in fstab file and password
# entries listed in /root/.nsmbrc file. See mount_smbfs(8) for details.
#

mount="/sbin/mount -o -N"
umount=/sbin/umount
HOME=/root; export HOME
vols=`awk -- '/^\/.*[[:space:]]+smbfs[[:space:]]+/ { print $2 }' /etc/fstab`

case "$1" in
start)
	echo -n "smbfs: "
	for vol in ${vols}; do
		$mount $vol
		echo -n "$vol "
	done
	;;
stop)
	echo -n "unmounting smbfs mount points: "
	for vol in ${vols}; do
		$umount $vol
		echo -n "$vol "
	done
	;;
*)
	echo "Usage: `basename $0` {start|stop}" >&2
	exit 64
esac

echo "Done"
