#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/granular/04.t 211352 2010-08-15 21:24:17Z pjd $

desc="NFSv4 granular permissions checking - ACL_WRITE_OWNER"

dir=`dirname $0`
. ${dir}/../misc.sh

nfsv4acls || quick_exit

echo "1..22"

n0=`namegen`
n2=`namegen`

expect 0 mkdir ${n2} 0755
cdir=`pwd`
cd ${n2}

# ACL_WRITE_OWNER permits to set gid to our own only.
expect 0 create ${n0} 0644
expect 0,0 lstat ${n0} uid,gid
expect EPERM -u 65534 -g 65532,65531 chown ${n0} -1 65532
expect 0,0 lstat ${n0} uid,gid
expect 0 prependacl ${n0} user:65534:write_owner::allow
expect EPERM -u 65534 -g 65532,65531 chown ${n0} -1 65530
expect 0,0 lstat ${n0} uid,gid
expect 0 -u 65534 -g 65532,65531 chown ${n0} -1 65532
expect 0,65532 lstat ${n0} uid,gid
expect 0 unlink ${n0}

# ACL_WRITE_OWNER permits to set uid to our own only.
expect 0 create ${n0} 0644
expect 0,0 lstat ${n0} uid,gid
expect EPERM -u 65534 -g 65532,65531 chown ${n0} 65534 65531
expect 0,0 lstat ${n0} uid,gid
expect 0 prependacl ${n0} user:65534:write_owner::allow
expect EPERM -u 65534 -g 65532,65531 chown ${n0} 65530 65531
expect 0,0 lstat ${n0} uid,gid
expect 0 -u 65534 -g 65532,65531 chown ${n0} 65534 65531
expect 65534,65531 lstat ${n0} uid,gid
expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n2}
