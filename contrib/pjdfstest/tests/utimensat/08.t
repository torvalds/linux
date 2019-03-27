#! /bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD$

desc="utimensat can set timestamps with subsecond precision"

dir=`dirname $0`
. ${dir}/../misc.sh

require "utimensat"

echo "1..9"

n0=`namegen`
n1=`namegen`
# Different file systems have different timestamp resolutions.  Check that they
# can do 0.1 second, but don't bother checking the finest resolution.
DATE1=100000000 #Sat Mar  3 02:46:40 MST 1973
DATE1_NS=100000000
DATE2=200000000 #Mon May  3 13:33:20 MDT 1976
DATE2_NS=200000000

expect 0 mkdir ${n1} 0755
cdir=`pwd`
cd ${n1}

create_file regular ${n0} 0644
expect 0 open . O_RDONLY : utimensat 0 ${n0} $DATE1 $DATE1_NS $DATE2 $DATE2_NS 0
expect $DATE1_NS lstat ${n0} atime_ns
expect $DATE2_NS lstat ${n0} mtime_ns
if supported "stat_st_birthtime"; then
	expect $DATE2_NS lstat ${n0} birthtime_ns
else
	test_check true
fi

expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n1}
