#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/ftruncate/00.t 219439 2011-03-09 23:11:30Z pjd $

desc="ftruncate descrease/increase file size"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..26"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n1} 0755
cdir=`pwd`
cd ${n1}

expect 0 create ${n0} 0644
expect 0 open ${n0} O_RDWR : ftruncate 0 1234567
expect 1234567 lstat ${n0} size
expect 0 open ${n0} O_WRONLY : ftruncate 0 567
expect 567 lstat ${n0} size
expect 0 unlink ${n0}

dd if=/dev/random of=${n0} bs=12345 count=1 >/dev/null 2>&1
expect 0 open ${n0} O_RDWR : ftruncate 0 23456
expect 23456 lstat ${n0} size
expect 0 open ${n0} O_WRONLY : ftruncate 0 1
expect 1 lstat ${n0} size
expect 0 unlink ${n0}

# successful ftruncate(2) updates ctime.
expect 0 create ${n0} 0644
ctime1=`${fstest} stat ${n0} ctime`
sleep 1
expect 0 open ${n0} O_RDWR : ftruncate 0 123
ctime2=`${fstest} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 unlink ${n0}

# unsuccessful ftruncate(2) does not update ctime.
expect 0 create ${n0} 0644
ctime1=`${fstest} stat ${n0} ctime`
sleep 1
expect EINVAL -u 65534 open ${n0} O_RDONLY : ftruncate 0 123
ctime2=`${fstest} stat ${n0} ctime`
test_check $ctime1 -eq $ctime2
expect 0 unlink ${n0}

# third argument should not affect permission.
expect 0 open ${n0} O_CREAT,O_RDWR 0 : ftruncate 0 0
expect 0 unlink ${n0}
expect 0 chmod . 0777
expect 0 -u 65534 open ${n0} O_CREAT,O_RDWR 0 : ftruncate 0 0
expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n1}
