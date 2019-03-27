#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chmod/12.t 219463 2011-03-10 20:59:02Z pjd $

desc="verify SUID/SGID bit behaviour"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..14"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n2} 0755
cdir=`pwd`
cd ${n2}

# Check whether writing to the file by non-owner clears the SUID.
expect 0 create ${n0} 04777
expect 0777 -u 65534 -g 65534 open ${n0} O_WRONLY : write 0 x : fstat 0 mode
expect 0777 stat ${n0} mode
expect 0 unlink ${n0}

# Check whether writing to the file by non-owner clears the SGID.
expect 0 create ${n0} 02777
expect 0777 -u 65534 -g 65534 open ${n0} O_RDWR : write 0 x : fstat 0 mode
expect 0777 stat ${n0} mode
expect 0 unlink ${n0}

# Check whether writing to the file by non-owner clears the SUID+SGID.
expect 0 create ${n0} 06777
expect 0777 -u 65534 -g 65534 open ${n0} O_RDWR : write 0 x : fstat 0 mode
expect 0777 stat ${n0} mode
expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n2}
