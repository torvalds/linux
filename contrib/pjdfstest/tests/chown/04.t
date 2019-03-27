#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chown/04.t 211410 2010-08-17 06:08:09Z pjd $

desc="chown returns ENOENT if the named file does not exist"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..9"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
expect ENOENT chown ${n0}/${n1}/test 65534 65534
expect ENOENT chown ${n0}/${n1} 65534 65534
expect ENOENT lchown ${n0}/${n1}/test 65534 65534
expect ENOENT lchown ${n0}/${n1} 65534 65534
expect 0 symlink ${n2} ${n0}/${n1}
expect ENOENT chown ${n0}/${n1} 65534 65534
expect 0 unlink ${n0}/${n1}
expect 0 rmdir ${n0}
