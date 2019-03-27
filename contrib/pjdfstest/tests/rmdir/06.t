#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/rmdir/06.t 211474 2010-08-18 22:06:43Z pjd $

desc="rmdir returns EEXIST or ENOTEMPTY the named directory contains files other than '.' and '..' in it"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..23"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
for type in regular dir fifo block char socket symlink; do
	create_file ${type} ${n0}/${n1}
	expect "EEXIST|ENOTEMPTY" rmdir ${n0}
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n0}/${n1}
	else
		expect 0 unlink ${n0}/${n1}
	fi
done
expect 0 rmdir ${n0}
