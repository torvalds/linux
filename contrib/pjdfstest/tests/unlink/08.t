#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/unlink/08.t 211352 2010-08-15 21:24:17Z pjd $

desc="unlink may return EPERM if the named file is a directory"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..3"

n0=`namegen`

expect 0 mkdir ${n0} 0755
todo Linux "According to POSIX: EPERM - The file named by path is a directory, and either the calling process does not have appropriate privileges, or the implementation prohibits using unlink() on directories."
expect "0|EPERM" unlink ${n0}
expect "0|ENOENT" rmdir ${n0}
