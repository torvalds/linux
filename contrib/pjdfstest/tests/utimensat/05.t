#! /bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD$

desc="utimensat can follow symlinks"

dir=`dirname $0`
. ${dir}/../misc.sh

require "utimensat"

echo "1..16"

n0=`namegen`
n1=`namegen`
n2=`namegen`
DATE1=1900000000 #Sun Mar 17 11:46:40 MDT 2030
DATE2=1950000000 #Fri Oct 17 04:40:00 MDT 2031
DATE3=1960000000 #Mon Feb  9 21:26:40 MST 2032
DATE4=1970000000 #Fri Jun  4 16:13:20 MDT 2032
DATE5=1980000000 #Tue Sep 28 10:00:00 MDT 2032
DATE6=1990000000 #Sat Jan 22 02:46:40 MST 2033

expect 0 mkdir ${n1} 0755
cdir=`pwd`
cd ${n1}


create_file regular ${n0}
ln -s ${n0} ${n2}
expect 0 open . O_RDONLY : utimensat 0 ${n0} $DATE1 0 $DATE2 0 0

expect 0 open . O_RDONLY : utimensat 0 ${n2} $DATE3 0 $DATE4 0 AT_SYMLINK_NOFOLLOW
expect $DATE1 lstat ${n0} atime
expect $DATE2 lstat ${n0} mtime
expect $DATE3 lstat ${n2} atime
expect $DATE4 lstat ${n2} mtime

expect 0 open . O_RDONLY : utimensat 0 ${n2} $DATE5 0 $DATE6 0 0
expect $DATE5 lstat ${n0} atime
expect $DATE6 lstat ${n0} mtime
# If atime is disabled on the current mount, then ${n2}'s atime should still be
# $DATE3.  However, if atime is enabled, then ${n2}'s atime will be the current
# system time.  For this test, it's sufficient to simply check that it didn't
# get set to DATE5
test_check "$DATE5" -ne `"$fstest" lstat ${n2} atime`
expect $DATE4 lstat ${n2} mtime

expect 0 unlink ${n0}
expect 0 unlink ${n2}

cd ${cdir}
expect 0 rmdir ${n1}
