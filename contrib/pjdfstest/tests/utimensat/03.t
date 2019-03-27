#! /bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD$

desc="utimensat can update birthtimes"

dir=`dirname $0`
. ${dir}/../misc.sh

require "utimensat"

require stat_st_birthtime

echo "1..12"

n0=`namegen`
n1=`namegen`
DATE1=100000000 #Sat Mar  3 02:46:40 MST 1973
DATE2=200000000 #Mon May  3 13:33:20 MDT 1976

expect 0 mkdir ${n1} 0755
cdir=`pwd`
cd ${n1}

create_file regular ${n0}
expect 0 open . O_RDONLY : utimensat 0 ${n0} $DATE1 0 $DATE1 0 0
expect $DATE1 lstat ${n0} atime
expect $DATE1 lstat ${n0} mtime
expect $DATE1 lstat ${n0} birthtime

expect 0 open . O_RDONLY : utimensat 0 ${n0} $DATE2 0 $DATE2 0 0
expect $DATE2 lstat ${n0} atime
expect $DATE2 lstat ${n0} mtime
expect $DATE1 lstat ${n0} birthtime

expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n1}
