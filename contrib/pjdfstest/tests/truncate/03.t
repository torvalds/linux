#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/truncate/03.t 211352 2010-08-15 21:24:17Z pjd $

desc="truncate returns ENAMETOOLONG if an entire path name exceeded {PATH_MAX} characters"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..5"

nx=`dirgen_max`
nxx="${nx}x"

mkdir -p "${nx%/*}"

expect 0 create ${nx} 0644
expect 0 truncate ${nx} 123
expect regular,123 stat ${nx} type,size
expect 0 unlink ${nx}
expect ENAMETOOLONG truncate ${nxx} 123

rm -rf "${nx%%/*}"
