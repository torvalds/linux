#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/link/15.t 211352 2010-08-15 21:24:17Z pjd $

desc="link returns ENOSPC if the directory in which the entry for the new link is being placed cannot be extended because there is no space left on the file system containing the directory"

dir=`dirname $0`
. ${dir}/../misc.sh

[ "${os}:${fs}" = "FreeBSD:UFS" ] || quick_exit

echo "1..4"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
n=`mdconfig -a -n -t malloc -s 512k` || exit
newfs /dev/md${n} >/dev/null || exit
mount /dev/md${n} ${n0} || exit
expect 0 create ${n0}/${n1} 0644
i=0
while :; do
	link ${n0}/${n1} ${n0}/${i} >/dev/null 2>&1
	if [ $? -ne 0 ]; then
		break
	fi
	i=`expr $i + 1`
done
expect ENOSPC link ${n0}/${n1} ${n0}/${n2}
umount /dev/md${n}
mdconfig -d -u ${n} || exit
expect 0 rmdir ${n0}
