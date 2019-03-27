#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/truncate/13.t 211352 2010-08-15 21:24:17Z pjd $

desc="ftruncate returns EINVAL if the length argument was less than 0"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..4"

n0=`namegen`

expect 0 create ${n0} 0644
expect EINVAL -- open ${n0} O_RDWR : ftruncate 0 -1
expect EINVAL -- open ${n0} O_WRONLY : ftruncate 0 -999999
expect 0 unlink ${n0}
