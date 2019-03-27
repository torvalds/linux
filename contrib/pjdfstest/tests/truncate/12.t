#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/truncate/12.t 211352 2010-08-15 21:24:17Z pjd $

desc="truncate returns EFBIG or EINVAL if the length argument was greater than the maximum file size"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..3"

n0=`namegen`

expect 0 create ${n0} 0644
r=`${fstest} truncate ${n0} 999999999999999 2>/dev/null`
case "${r}" in
EFBIG|EINVAL)
	expect 0 stat ${n0} size
	;;
0)
	expect 999999999999999 stat ${n0} size
	;;
*)
	echo "not ok ${ntest}"
	ntest=`expr ${ntest} + 1`
	;;
esac
expect 0 unlink ${n0}
