#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/link/14.t 211352 2010-08-15 21:24:17Z pjd $

desc="link returns EXDEV if the source and the destination files are on different file systems"

dir=`dirname $0`
. ${dir}/../misc.sh

[ "${os}" = "FreeBSD" ] || quick_exit

echo "1..8"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
n=`mdconfig -a -n -t malloc -s 1m` || exit
newfs /dev/md${n} >/dev/null || exit
mount /dev/md${n} ${n0} || exit
expect 0 create ${n0}/${n1} 0644
expect EXDEV link ${n0}/${n1} ${n2}
expect 0 unlink ${n0}/${n1}
expect 0 create ${n1} 0644
expect EXDEV link ${n1} ${n0}/${n2}
expect 0 unlink ${n1}
umount /dev/md${n}
mdconfig -d -u ${n} || exit
expect 0 rmdir ${n0}
