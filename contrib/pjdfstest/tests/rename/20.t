#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/rename/20.t 211352 2010-08-15 21:24:17Z pjd $

desc="rename returns EEXIST or ENOTEMPTY if the 'to' argument is a directory and is not empty"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..25"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
expect 0 mkdir ${n1} 0755

for type in regular dir fifo block char socket symlink; do
	create_file ${type} ${n1}/${n2}
	expect "EEXIST|ENOTEMPTY" rename ${n0} ${n1}
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n1}/${n2}
	else
		expect 0 unlink ${n1}/${n2}
	fi
done

expect 0 rmdir ${n1}
expect 0 rmdir ${n0}
