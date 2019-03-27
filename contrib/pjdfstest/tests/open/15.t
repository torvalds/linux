#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/open/15.t 211352 2010-08-15 21:24:17Z pjd $

desc="open returns EROFS when O_CREAT is specified and the named file would reside on a read-only file system"

dir=`dirname $0`
. ${dir}/../misc.sh

[ "${os}:${fs}" = "FreeBSD:UFS" ] || quick_exit

echo "1..5"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
n=`mdconfig -a -n -t malloc -s 1m` || exit
newfs /dev/md${n} >/dev/null || exit
mount /dev/md${n} ${n0} || exit
expect 0 open ${n0}/${n1} O_RDONLY,O_CREAT 0644
expect 0 unlink ${n0}/${n1}
mount -ur /dev/md${n}
expect EROFS open ${n0}/${n1} O_RDONLY,O_CREAT 0644
mount -uw /dev/md${n}
umount /dev/md${n}
mdconfig -d -u ${n} || exit
expect 0 rmdir ${n0}
