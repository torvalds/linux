#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/mkdir/03.t 211352 2010-08-15 21:24:17Z pjd $

desc="mkdir returns ENAMETOOLONG if an entire path name exceeded {PATH_MAX} characters"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..3"

nx=`dirgen_max`
nxx="${nx}x"

mkdir -p "${nx%/*}"

expect 0 mkdir ${nx} 0755
expect 0 rmdir ${nx}
expect ENAMETOOLONG mkdir ${nxx} 0755

rm -rf "${nx%%/*}"
