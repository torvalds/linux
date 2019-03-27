#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/link/10.t 211474 2010-08-18 22:06:43Z pjd $

desc="link returns EEXIST if the destination file does exist"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..23"

n0=`namegen`
n1=`namegen`

expect 0 create ${n0} 0644

for type in regular dir fifo block char socket symlink; do
	create_file ${type} ${n1}
	expect EEXIST link ${n0} ${n1}
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n1}
	else
		expect 0 unlink ${n1}
	fi
done

expect 0 unlink ${n0}
