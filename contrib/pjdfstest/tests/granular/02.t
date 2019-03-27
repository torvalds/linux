#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/granular/02.t 211352 2010-08-15 21:24:17Z pjd $

desc="NFSv4 granular permissions checking - ACL_READ_ACL and ACL_WRITE_ACL"

dir=`dirname $0`
. ${dir}/../misc.sh

[ "${os}:${fs}" = "FreeBSD:ZFS" ] || quick_exit

echo "1..83"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n2} 0755
cdir=`pwd`
cd ${n2}

# Check whether user 65534 is permitted to read ACL.
expect 0 create ${n0} 0644
expect 0 readacl ${n0}
expect 0 -u 65534 -g 65534 readacl ${n0}
expect 0 prependacl ${n0} user:65534:read_acl::deny
expect 0 readacl ${n0}
expect EACCES -u 65534 -g 65534 readacl ${n0}
expect 0 prependacl ${n0} user:65534:read_acl::allow
expect 0 -u 65534 -g 65534 readacl ${n0}
expect 0 readacl ${n0}
expect 0 unlink ${n0}

# Check whether user 65534 is permitted to write ACL.
expect 0 create ${n0} 0644
expect EPERM -u 65534 -g 65534 prependacl ${n0} user:65534:read_data::allow
expect 0 prependacl ${n0} user:65534:write_acl::allow
expect 0 -u 65534 -g 65534 prependacl ${n0} user:65534:read_data::allow
expect 0 unlink ${n0}

# Check whether user 65534 is permitted to write mode.
expect 0 create ${n0} 0755
expect EPERM -u 65534 -g 65534 chmod ${n0} 0777
expect 0 prependacl ${n0} user:65534:write_acl::allow
expect 0 -u 65534 -g 65534 chmod ${n0} 0777
expect 0 unlink ${n0}

# There is an interesting problem with interaction between ACL_WRITE_ACL
# and SUID/SGID bits.  In case user does have ACL_WRITE_ACL, but is not
# a file owner, Solaris does the following:
# 1. Setting SUID fails with EPERM.
# 2. Setting SGID succeeds, but mode is not changed.
# 3. Modifying ACL does not clear SUID nor SGID bits.
# 4. Writing the file does clear both SUID and SGID bits.
#
# What we are doing is the following:
# 1. Setting SUID or SGID fails with EPERM.
# 2. Modifying ACL does not clear SUID nor SGID bits.
# 3. Writing the file does clear both SUID and SGID bits.
#
# Check whether user 65534 is denied to write mode with SUID bit.
expect 0 create ${n0} 0755
expect EPERM -u 65534 -g 65534 chmod ${n0} 04777
expect 0 prependacl ${n0} user:65534:write_acl::allow
expect EPERM -u 65534 -g 65534 chmod ${n0} 04777
expect 0 unlink ${n0}

# Check whether user 65534 is denied to write mode with SGID bit.
expect 0 create ${n0} 0755
expect EPERM -u 65534 -g 65534 chmod ${n0} 02777
expect 0 prependacl ${n0} user:65534:write_acl::allow
expect EPERM -u 65534 -g 65534 chmod ${n0} 02777
expect 0 unlink ${n0}

# Check whether user 65534 is allowed to write mode with sticky bit.
expect 0 mkdir ${n0} 0755
expect EPERM -u 65534 -g 65534 chmod ${n0} 01777
expect 0 prependacl ${n0} user:65534:write_acl::allow
expect 0 -u 65534 -g 65534 chmod ${n0} 01777
expect 0 rmdir ${n0}

# Check whether modifying the ACL by not-owner preserves the SUID.
expect 0 create ${n0} 04755
expect 0 prependacl ${n0} user:65534:write_acl::allow
expect 0 -u 65534 -g 65534 prependacl ${n0} user:65534:write_data::allow
expect 04755 stat ${n0} mode
expect 0 unlink ${n0}

# Check whether modifying the ACL by not-owner preserves the SGID.
expect 0 create ${n0} 02755
expect 0 prependacl ${n0} user:65534:write_acl::allow
expect 0 -u 65534 -g 65534 prependacl ${n0} user:65534:write_data::allow
expect 02755 stat ${n0} mode
expect 0 unlink ${n0}

# Check whether modifying the ACL by not-owner preserves the sticky bit.
expect 0 mkdir ${n0} 0755
expect 0 chmod ${n0} 01755
expect 0 prependacl ${n0} user:65534:write_acl::allow
expect 0 -u 65534 -g 65534 prependacl ${n0} user:65534:write_data::allow
expect 01755 stat ${n0} mode
expect 0 rmdir ${n0}

# Clearing the SUID and SGID bits when being written to by non-owner
# is checked in chmod/12.t.

# Check whether the file owner is always permitted to get and set
# ACL and file mode, even if ACL_{READ,WRITE}_ACL would deny it.
expect 0 chmod . 0777
expect 0 -u 65534 -g 65534 create ${n0} 0600
expect 0 -u 65534 -g 65534 prependacl ${n0} user:65534:write_acl::deny
expect 0 -u 65534 -g 65534 prependacl ${n0} user:65534:read_acl::deny
expect 0 -u 65534 -g 65534 readacl ${n0}
expect 0600 -u 65534 -g 65534 stat ${n0} mode
expect 0 -u 65534 -g 65534 chmod ${n0} 0777
expect 0 unlink ${n0}

expect 0 -u 65534 -g 65534 mkdir ${n0} 0600
expect 0 -u 65534 -g 65534 prependacl ${n0} user:65534:write_acl::deny
expect 0 -u 65534 -g 65534 prependacl ${n0} user:65534:read_acl::deny
expect 0 -u 65534 -g 65534 readacl ${n0}
expect 0600 -u 65534 -g 65534 stat ${n0} mode
expect 0 -u 65534 -g 65534 chmod ${n0} 0777
expect 0 rmdir ${n0}

# Check whether the root is allowed for these as well.
expect 0 -u 65534 -g 65534 create ${n0} 0600
expect 0 prependacl ${n0} everyone@:write_acl::deny
expect 0 prependacl ${n0} everyone@:read_acl::deny
expect 0 readacl ${n0}
expect 0600 stat ${n0} mode
expect 0 chmod ${n0} 0777
expect 0 unlink ${n0}

expect 0 -u 65534 -g 65534 mkdir ${n0} 0600
expect 0 prependacl ${n0} everyone@:write_acl::deny
expect 0 prependacl ${n0} everyone@:read_acl::deny
expect 0600 stat ${n0} mode
expect 0 readacl ${n0}
expect 0600 stat ${n0} mode
expect 0 chmod ${n0} 0777
expect 0 rmdir ${n0}

cd ${cdir}
expect 0 rmdir ${n2}
