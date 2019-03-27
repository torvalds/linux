#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chown/06.t 211410 2010-08-17 06:08:09Z pjd $

desc="chown returns ELOOP if too many symbolic links were encountered in translating the pathname"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..10"

n0=`namegen`
n1=`namegen`

expect 0 symlink ${n0} ${n1}
expect 0 symlink ${n1} ${n0}
expect ELOOP chown ${n0} 65534 65534
expect ELOOP chown ${n1} 65534 65534
expect ELOOP chown ${n0}/test 65534 65534
expect ELOOP chown ${n1}/test 65534 65534
expect ELOOP lchown ${n0}/test 65534 65534
expect ELOOP lchown ${n1}/test 65534 65534
expect 0 unlink ${n0}
expect 0 unlink ${n1}
