#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/rename/18.t 211352 2010-08-15 21:24:17Z pjd $

desc="rename returns EINVAL when the 'from' argument is a parent directory of 'to'"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..6"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
expect 0 mkdir ${n0}/${n1} 0755

expect EINVAL rename ${n0} ${n0}/${n1}
expect EINVAL rename ${n0} ${n0}/${n1}/${n2}

expect 0 rmdir ${n0}/${n1}
expect 0 rmdir ${n0}
