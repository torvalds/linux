#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/rmdir/01.t 211352 2010-08-15 21:24:17Z pjd $

desc="rmdir returns ENOTDIR if a component of the path is not a directory"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..14"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
expect 0 create ${n0}/${n1} 0644
expect ENOTDIR rmdir ${n0}/${n1}/test
expect 0 unlink ${n0}/${n1}
expect 0 rmdir ${n0}

expect 0 create ${n0} 0644
expect ENOTDIR rmdir ${n0}
expect 0 unlink ${n0}

expect 0 symlink ${n1} ${n0}
expect ENOTDIR rmdir ${n0}
expect 0 unlink ${n0}

expect 0 mkfifo ${n0} 0644
expect ENOTDIR rmdir ${n0}
expect 0 unlink ${n0}
