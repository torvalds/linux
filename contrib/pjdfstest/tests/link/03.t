#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/link/03.t 211352 2010-08-15 21:24:17Z pjd $

desc="link returns ENAMETOOLONG if an entire length of either path name exceeded {PATH_MAX} characters"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..13"

n0=`namegen`
nx=`dirgen_max`
nxx="${nx}x"

mkdir -p "${nx%/*}"

expect 0 create ${nx} 0644
expect 0 link ${nx} ${n0}
expect 2 stat ${n0} nlink
expect 2 stat ${nx} nlink
expect 0 unlink ${nx}
expect 0 link ${n0} ${nx}
expect 2 stat ${n0} nlink
expect 2 stat ${nx} nlink
expect 0 unlink ${nx}
expect ENAMETOOLONG link ${n0} ${nxx}
expect 1 stat ${n0} nlink
expect 0 unlink ${n0}
expect ENAMETOOLONG link ${nxx} ${n0}

rm -rf "${nx%%/*}"
