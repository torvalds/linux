#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/rmdir/04.t 211352 2010-08-15 21:24:17Z pjd $

desc="rmdir returns ENOENT if the named directory does not exist"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..4"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
expect 0 rmdir ${n0}
expect ENOENT rmdir ${n0}
expect ENOENT rmdir ${n1}
