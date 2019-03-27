#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/rename/15.t 211352 2010-08-15 21:24:17Z pjd $

desc="rename returns EXDEV if the link named by 'to' and the file named by 'from' are on different file systems"

dir=`dirname $0`
. ${dir}/../misc.sh

[ "${os}" = "FreeBSD" ] || quick_exit

echo "1..23"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
n=`mdconfig -a -n -t malloc -s 1m` || exit
newfs /dev/md${n} >/dev/null || exit
mount /dev/md${n} ${n0} || exit

for type in regular dir fifo block char socket symlink; do
	create_file ${type} ${n0}/${n1}
	expect EXDEV rename ${n0}/${n1} ${n2}
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n0}/${n1}
	else
		expect 0 unlink ${n0}/${n1}
	fi
done

umount /dev/md${n}
mdconfig -d -u ${n} || exit
expect 0 rmdir ${n0}
