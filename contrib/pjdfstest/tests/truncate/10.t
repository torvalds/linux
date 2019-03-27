#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/truncate/10.t 211352 2010-08-15 21:24:17Z pjd $

desc="truncate returns EROFS if the named file resides on a read-only file system"

dir=`dirname $0`
. ${dir}/../misc.sh

[ "${os}:${fs}" = "FreeBSD:UFS" ] || quick_exit

echo "1..10"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
n=`mdconfig -a -n -t malloc -s 1m` || exit
newfs /dev/md${n} >/dev/null || exit
mount /dev/md${n} ${n0} || exit
expect 0 create ${n0}/${n1} 0644
expect 0 truncate ${n0}/${n1} 123
expect 123 stat ${n0}/${n1} size
mount -ur /dev/md${n}
expect EROFS truncate ${n0}/${n1} 1234
expect 123 stat ${n0}/${n1} size
mount -uw /dev/md${n}
expect 0 truncate ${n0}/${n1} 1234
expect 1234 stat ${n0}/${n1} size
expect 0 unlink ${n0}/${n1}
umount /dev/md${n}
mdconfig -d -u ${n} || exit
expect 0 rmdir ${n0}
