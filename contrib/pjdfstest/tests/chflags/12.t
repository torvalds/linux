#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chflags/12.t 211352 2010-08-15 21:24:17Z pjd $

desc="chflags returns EROFS if the named file resides on a read-only file system"

dir=`dirname $0`
. ${dir}/../misc.sh

require chflags

case "${os}:${fs}" in
FreeBSD:UFS)
	echo "1..14"

	n0=`namegen`
	n1=`namegen`

	expect 0 mkdir ${n0} 0755
	n=`mdconfig -a -n -t malloc -s 1m` || exit
	newfs /dev/md${n} >/dev/null || exit
	mount /dev/md${n} ${n0}
	expect 0 create ${n0}/${n1} 0644
	expect 0 chflags ${n0}/${n1} UF_IMMUTABLE
	expect UF_IMMUTABLE stat ${n0}/${n1} flags
	expect 0 chflags ${n0}/${n1} none
	expect none stat ${n0}/${n1} flags
	mount -ur /dev/md${n}
	expect EROFS chflags ${n0}/${n1} UF_IMMUTABLE
	expect none stat ${n0}/${n1} flags
	mount -uw /dev/md${n}
	expect 0 chflags ${n0}/${n1} UF_IMMUTABLE
	expect UF_IMMUTABLE stat ${n0}/${n1} flags
	expect 0 chflags ${n0}/${n1} none
	expect none stat ${n0}/${n1} flags
	expect 0 unlink ${n0}/${n1}
	umount /dev/md${n}
	mdconfig -d -u ${n} || exit
	expect 0 rmdir ${n0}
	;;
FreeBSD:ZFS)
	echo "1..12"

	n0=`namegen`
	n1=`namegen`

	n=`mdconfig -a -n -t malloc -s 128m` || exit
	zpool create ${n0} /dev/md${n}
	expect 0 create /${n0}/${n1} 0644
	expect 0 chflags /${n0}/${n1} UF_NODUMP
	expect UF_NODUMP stat /${n0}/${n1} flags
	expect 0 chflags /${n0}/${n1} none
	expect none stat /${n0}/${n1} flags
	zfs set readonly=on ${n0}
	expect EROFS chflags /${n0}/${n1} UF_NODUMP
	expect none stat /${n0}/${n1} flags
	zfs set readonly=off ${n0}
	expect 0 chflags /${n0}/${n1} UF_NODUMP
	expect UF_NODUMP stat /${n0}/${n1} flags
	expect 0 chflags /${n0}/${n1} none
	expect none stat /${n0}/${n1} flags
	expect 0 unlink /${n0}/${n1}
	zpool destroy ${n0}
	mdconfig -d -u ${n} || exit
	;;
*)
	quick_exit
	;;
esac
