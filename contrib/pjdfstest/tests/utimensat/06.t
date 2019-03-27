#! /bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD$

desc="utimensat with UTIME_NOW will work if the caller has write permission"

dir=`dirname $0`
. ${dir}/../misc.sh

require "utimensat"

echo "1..13"

n0=`namegen`
n1=`namegen`
UID_NOBODY=`id -u nobody`
GID_NOBODY=`id -g nobody`
UID_ROOT=`id -u root`
GID_ROOT=`id -g root`

expect 0 mkdir ${n1} 0755
cdir=`pwd`
cd ${n1}

create_file regular ${n0} 0644
# First check that nobody can't update the timestamps
expect EACCES -u $UID_NOBODY open . O_RDONLY : utimensat 0 ${n0} 0 UTIME_NOW 0 UTIME_NOW 0

# Now check that the owner can update the timestamps
expect 0 chown ${n0} $UID_NOBODY $GID_NOBODY
expect 0 chmod ${n0} 0444
expect 0 -u $UID_NOBODY open . O_RDONLY : utimensat 0 ${n0} 0 UTIME_NOW 0 UTIME_NOW 0

# Now check that the superuser can update the timestamps
expect 0 -u $UID_ROOT open . O_RDONLY : utimensat 0 ${n0} 0 UTIME_OMIT 0 UTIME_OMIT 0

# Now check that anyone with write permission can update the timestamps
expect 0 chown ${n0} $UID_ROOT $GID_ROOT
expect 0 chmod ${n0} 0666
expect 0 -u $UID_NOBODY open . O_RDONLY : utimensat 0 ${n0} 0 UTIME_NOW 0 UTIME_NOW 0

expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n1}
