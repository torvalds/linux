#! /bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD$

desc="utimensat with UTIME_OMIT will leave the time unchanged"

dir=`dirname $0`
. ${dir}/../misc.sh

require "utimensat"

echo "1..10"

n0=`namegen`
n1=`namegen`
DATE1=1900000000 #Sun Mar 17 11:46:40 MDT 2030
DATE2=1950000000 #Fri Oct 17 04:40:00 MDT 2031

expect 0 mkdir ${n1} 0755
cdir=`pwd`
cd ${n1}

create_file regular ${n0}
orig_mtime=`$fstest lstat ${n0} mtime`
expect 0 open . O_RDONLY : utimensat 0 ${n0} $DATE1 0 0 UTIME_OMIT 0
expect $DATE1 lstat ${n0} atime
expect $orig_mtime lstat ${n0} mtime

expect 0 open . O_RDONLY : utimensat 0 ${n0} 0 UTIME_OMIT $DATE2 0 0
expect $DATE1 lstat ${n0} atime
expect $DATE2 lstat ${n0} mtime
expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n1}
