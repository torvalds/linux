#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chmod/06.t 211474 2010-08-18 22:06:43Z pjd $

desc="chmod returns ELOOP if too many symbolic links were encountered in translating the pathname"

dir=`dirname $0`
. ${dir}/../misc.sh

if supported lchmod; then
	echo "1..10"
else
	echo "1..8"
fi

n0=`namegen`
n1=`namegen`

expect 0 symlink ${n0} ${n1}
expect 0 symlink ${n1} ${n0}
expect ELOOP chmod ${n0} 0644
expect ELOOP chmod ${n1} 0644
expect ELOOP chmod ${n0}/test 0644
expect ELOOP chmod ${n1}/test 0644
if supported lchmod; then
	expect ELOOP lchmod ${n0}/test 0644
	expect ELOOP lchmod ${n1}/test 0644
fi
expect 0 unlink ${n0}
expect 0 unlink ${n1}
