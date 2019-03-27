#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chmod/03.t 211352 2010-08-15 21:24:17Z pjd $

desc="chmod returns ENAMETOOLONG if an entire path name exceeded {PATH_MAX} characters"

dir=`dirname $0`
. ${dir}/../misc.sh

if supported lchmod; then
	echo "1..10"
else
	echo "1..5"
fi

nx=`dirgen_max`
nxx="${nx}x"

mkdir -p "${nx%/*}"

expect 0 create ${nx} 0644
expect 0 chmod ${nx} 0642
expect 0642 stat ${nx} mode
expect 0 unlink ${nx}
expect ENAMETOOLONG chmod ${nxx} 0642

if supported lchmod; then
	expect 0 create ${nx} 0644
	expect 0 lchmod ${nx} 0642
	expect 0642 stat ${nx} mode
	expect 0 unlink ${nx}
	expect ENAMETOOLONG lchmod ${nxx} 0642
fi

rm -rf "${nx%%/*}"
