#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/mkfifo/03.t 211352 2010-08-15 21:24:17Z pjd $

desc="mkfifo returns ENAMETOOLONG if an entire path name exceeded {PATH_MAX} characters"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..4"

nx=`dirgen_max`
nxx="${nx}x"

mkdir -p "${nx%/*}"

expect 0 mkfifo ${nx} 0644
expect fifo stat ${nx} type
expect 0 unlink ${nx}
expect ENAMETOOLONG mkfifo ${nxx} 0644

rm -rf "${nx%%/*}"
