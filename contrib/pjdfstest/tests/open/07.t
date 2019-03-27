#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/open/07.t 211352 2010-08-15 21:24:17Z pjd $

desc="open returns EACCES when O_TRUNC is specified and write permission is denied"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..23"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
expect 0 chown ${n0} 65534 65534
cdir=`pwd`
cd ${n0}

expect 0 -u 65534 -g 65534 create ${n1} 0644

expect 0 -u 65534 -g 65534 chmod ${n1} 0477
expect EACCES -u 65534 -g 65534 open ${n1} O_RDONLY,O_TRUNC
expect 0 -u 65534 -g 65534 chmod ${n1} 0747
expect EACCES -u 65533 -g 65534 open ${n1} O_RDONLY,O_TRUNC
expect 0 -u 65534 -g 65534 chmod ${n1} 0774
expect EACCES -u 65533 -g 65533 open ${n1} O_RDONLY,O_TRUNC

expect 0 -u 65534 -g 65534 chmod ${n1} 0177
expect EACCES -u 65534 -g 65534 open ${n1} O_RDONLY,O_TRUNC
expect 0 -u 65534 -g 65534 chmod ${n1} 0717
expect EACCES -u 65533 -g 65534 open ${n1} O_RDONLY,O_TRUNC
expect 0 -u 65534 -g 65534 chmod ${n1} 0771
expect EACCES -u 65533 -g 65533 open ${n1} O_RDONLY,O_TRUNC

expect 0 -u 65534 -g 65534 chmod ${n1} 0077
expect EACCES -u 65534 -g 65534 open ${n1} O_RDONLY,O_TRUNC
expect 0 -u 65534 -g 65534 chmod ${n1} 0707
expect EACCES -u 65533 -g 65534 open ${n1} O_RDONLY,O_TRUNC
expect 0 -u 65534 -g 65534 chmod ${n1} 0770
expect EACCES -u 65533 -g 65533 open ${n1} O_RDONLY,O_TRUNC

expect 0 -u 65534 -g 65534 unlink ${n1}

cd ${cdir}
expect 0 rmdir ${n0}
