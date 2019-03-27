#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/open/23.t 211352 2010-08-15 21:24:17Z pjd $

desc="open may return EINVAL when an attempt was made to open a descriptor with an illegal combination of O_RDONLY, O_WRONLY, and O_RDWR"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..5"

n0=`namegen`

expect 0 create ${n0} 0644
expect "0|EINVAL" open ${n0} O_RDONLY,O_RDWR
expect "0|EINVAL" open ${n0} O_WRONLY,O_RDWR
expect "0|EINVAL" open ${n0} O_RDONLY,O_WRONLY,O_RDWR
expect 0 unlink ${n0}
