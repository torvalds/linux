#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/granular/01.t 211352 2010-08-15 21:24:17Z pjd $

desc="NFSv4 granular permissions checking - ACL_READ_ATTRIBUTES and ACL_WRITE_ATTRIBUTES"

dir=`dirname $0`
. ${dir}/../misc.sh

[ "${os}:${fs}" = "FreeBSD:ZFS" ] || quick_exit

echo "1..12"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n2} 0755
cdir=`pwd`
cd ${n2}

# Tests 1..12 - check out whether user 65534 is permitted to read attributes.
expect 0 create ${n0} 0644
expect 0 lstat ${n0} size
expect 0 -u 65534 -g 65534 stat ${n0} size
expect 0 prependacl ${n0} user:65534:read_attributes::deny
expect 0 lstat ${n0} size
expect EACCES -u 65534 -g 65534 stat ${n0} size
expect 0 prependacl ${n0} user:65534:read_attributes::allow
expect 0 -u 65534 -g 65534 stat ${n0} size
expect 0 lstat ${n0} size
expect 0 unlink ${n0}

# Tests 12..12 - check out whether user 65534 is permitted to write attributes.
# XXX: Check if ACL_WRITE_ATTRIBUTES allows for modifying access times.

cd ${cdir}
expect 0 rmdir ${n2}
