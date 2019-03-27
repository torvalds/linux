#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/symlink/12.t 211352 2010-08-15 21:24:17Z pjd $

desc="symlink returns EFAULT if one of the pathnames specified is outside the process's allocated address space"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..6"

n0=`namegen`

expect EFAULT symlink NULL ${n0}
expect EFAULT symlink DEADCODE ${n0}
expect EFAULT symlink test NULL
expect EFAULT symlink test DEADCODE
expect EFAULT symlink NULL DEADCODE
expect EFAULT symlink DEADCODE NULL
