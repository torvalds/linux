#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chmod/09.t 211352 2010-08-15 21:24:17Z pjd $

desc="chmod returns EROFS if the named file resides on a read-only file system"

dir=`dirname $0`
. ${dir}/../misc.sh

[ "${os}:${fs}" = "FreeBSD:UFS" ] || quick_exit

echo "1..15"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
n=`mdconfig -a -n -t malloc -s 1m` || exit
newfs /dev/md${n} >/dev/null || exit
mount /dev/md${n} ${n0} || exit
expect 0 create ${n0}/${n1} 0644
expect 0 chmod ${n0}/${n1} 0640
expect 0640 stat ${n0}/${n1} mode
expect 0 lchmod ${n0}/${n1} 0530
expect 0530 stat ${n0}/${n1} mode
mount -ur /dev/md${n}
expect EROFS chmod ${n0}/${n1} 0600
expect EROFS lchmod ${n0}/${n1} 0600
expect 0530 stat ${n0}/${n1} mode
mount -uw /dev/md${n}
expect 0 chmod ${n0}/${n1} 0600
expect 0600 stat ${n0}/${n1} mode
expect 0 lchmod ${n0}/${n1} 0640
expect 0640 stat ${n0}/${n1} mode
expect 0 unlink ${n0}/${n1}
umount /dev/md${n}
mdconfig -d -u ${n} || exit
expect 0 rmdir ${n0}
