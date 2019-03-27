#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/symlink/00.t 211352 2010-08-15 21:24:17Z pjd $

desc="symlink creates symbolic links"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..14"

n0=`namegen`
n1=`namegen`

expect 0 create ${n0} 0644
expect regular,0644 lstat ${n0} type,mode
expect 0 symlink ${n0} ${n1}
expect symlink lstat ${n1} type
expect regular,0644 stat ${n1} type,mode
expect 0 unlink ${n0}
expect ENOENT stat ${n1} type,mode
expect 0 unlink ${n1}

expect 0 mkdir ${n0} 0755
time=`${fstest} stat ${n0} ctime`
sleep 1
expect 0 symlink test ${n0}/${n1}
mtime=`${fstest} stat ${n0} mtime`
test_check $time -lt $mtime
ctime=`${fstest} stat ${n0} ctime`
test_check $time -lt $ctime
expect 0 unlink ${n0}/${n1}
expect 0 rmdir ${n0}
