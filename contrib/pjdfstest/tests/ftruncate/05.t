#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/truncate/05.t 211352 2010-08-15 21:24:17Z pjd $

desc="truncate returns EACCES when search permission is denied for a component of the path prefix"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..15"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
cdir=`pwd`
cd ${n0}
expect 0 mkdir ${n1} 0755
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 create ${n1}/${n2} 0644
expect 0 -u 65534 -g 65534 truncate ${n1}/${n2} 123
expect 123 -u 65534 -g 65534 stat ${n1}/${n2} size
expect 0 chmod ${n1} 0644
expect EACCES -u 65534 -g 65534 truncate ${n1}/${n2} 1234
expect 0 chmod ${n1} 0755
expect 123 -u 65534 -g 65534 stat ${n1}/${n2} size
expect 0 -u 65534 -g 65534 truncate ${n1}/${n2} 1234
expect 1234 -u 65534 -g 65534 stat ${n1}/${n2} size
expect 0 -u 65534 -g 65534 unlink ${n1}/${n2}
expect 0 rmdir ${n1}
cd ${cdir}
expect 0 rmdir ${n0}
