#! /bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD$

desc="utimensat is y2038 compliant"

dir=`dirname $0`
. ${dir}/../misc.sh

require "utimensat"

echo "1..7"

require utimensat

n0=`namegen`
n1=`namegen`
DATE1=2147483648	# 2^31, ie Mon Jan 18 20:14:08 MST 2038
DATE2=4294967296	# 2^32, ie Sat Feb  6 23:28:16 MST 2106

expect 0 mkdir ${n1} 0755
cdir=`pwd`
cd ${n1}


create_file regular ${n0}
expect 0 open . O_RDONLY : utimensat 0 ${n0} $DATE1 0 $DATE2 0 0
expect $DATE1 lstat ${n0} atime
expect $DATE2 lstat ${n0} mtime


expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n1}
