#! /bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD$

desc="utimensat will work if the caller is the owner or root"

dir=`dirname $0`
. ${dir}/../misc.sh

require "utimensat"

echo "1..17"

n0=`namegen`
n1=`namegen`
DATE1=1900000000 #Sun Mar 17 11:46:40 MDT 2030
DATE2=1950000000 #Fri Oct 17 04:40:00 MDT 2031
UID_NOBODY=`id -u nobody`
GID_NOBODY=`id -g nobody`
UID_ROOT=`id -u root`
GID_ROOT=`id -g root`

expect 0 mkdir ${n1} 0755
cdir=`pwd`
cd ${n1}

create_file regular ${n0} 0644 $UID_ROOT $GID_ROOT
# First check that nobody can't update the timestamps
expect EPERM -u $UID_NOBODY open . O_RDONLY : utimensat 0 ${n0} 0 UTIME_OMIT $DATE2 0 0
expect EPERM -u $UID_NOBODY open . O_RDONLY : utimensat 0 ${n0} $DATE1 0 0 UTIME_OMIT 0
expect EPERM -u $UID_NOBODY open . O_RDONLY : utimensat 0 ${n0} $DATE1 0 $DATE2 0 0

# Now check that a nonowner with write permission can't update the timestamps
expect 0 chmod ${n0} 0666
expect EPERM -u $UID_NOBODY open . O_RDONLY : utimensat 0 ${n0} 0 UTIME_OMIT $DATE2 0 0
expect EPERM -u $UID_NOBODY open . O_RDONLY : utimensat 0 ${n0} $DATE1 0 0 UTIME_OMIT 0
expect EPERM -u $UID_NOBODY open . O_RDONLY : utimensat 0 ${n0} $DATE1 0 $DATE2 0 0

# Now check that the owner can update the timestamps
expect 0 chown ${n0} $UID_NOBODY $GID_NOBODY
expect 0 chmod ${n0} 0444
expect 0 -u $UID_NOBODY open . O_RDONLY : utimensat 0 ${n0} $DATE1 0 $DATE2 0

# Now check that the superuser can update the timestamps
expect 0 -u $UID_ROOT open . O_RDONLY : utimensat 0 ${n0} $DATE1 0 $DATE2 0 0


expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n1}
