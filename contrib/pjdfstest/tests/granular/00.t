#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/granular/00.t 211352 2010-08-15 21:24:17Z pjd $

desc="NFSv4 granular permissions checking - WRITE_DATA vs APPEND_DATA on directories"

dir=`dirname $0`
. ${dir}/../misc.sh

[ "${os}:${fs}" = "FreeBSD:ZFS" ] || quick_exit

echo "1..49"

n0=`namegen`
n1=`namegen`
n2=`namegen`
n3=`namegen`

expect 0 mkdir ${n2} 0755
expect 0 mkdir ${n3} 0777
cdir=`pwd`
cd ${n2}

# Tests 2..7 - check out whether root user can do stuff.
# Can create files?
expect 0 create ${n0} 0644

# Can create symlinks?
expect 0 link ${n0} ${n1}
expect 0 unlink ${n1}
expect 0 unlink ${n0}

# Can create directories?
expect 0 mkdir ${n0} 0755
expect 0 rmdir ${n0}

# Check whether user 65534 is permitted to create and remove
# files, but not subdirectories.
expect 0 prependacl . user:65534:write_data::allow,user:65534:append_data::deny

# Can create files?
expect 0 -u 65534 -g 65534 create ${n0} 0644

# Can create symlinks?
expect 0 -u 65534 -g 65534 link ${n0} ${n1}
expect 0 -u 65534 -g 65534 unlink ${n1}
expect 0 -u 65534 -g 65534 unlink ${n0}

# Can create directories?
expect EACCES -u 65534 -g 65534 mkdir ${n0} 0755
expect ENOENT -u 65534 -g 65534 rmdir ${n0}
expect 0 mkdir ${n0} 0755
expect 0 -u 65534 -g 65534 rmdir ${n0}

# Can move files from other directory?
expect 0 create ../${n3}/${n1} 0644
expect 0 -u 65534 -g 65534 rename ../${n3}/${n1} ${n0}

# Can move files from other directory overwriting existing files?
expect 0 create ../${n3}/${n1} 0644
expect 0 -u 65534 -g 65534 rename ../${n3}/${n1} ${n0}

expect 0 -u 65534 -g 65534 unlink ${n0}

# Can move directories from other directory?
expect 0 mkdir ../${n3}/${n1} 0777
expect EACCES -u 65534 -g 65534 rename ../${n3}/${n1} ${n0}

# Can move directories from other directory overwriting existing directory?
expect EACCES -u 65534 -g 65534 rename ../${n3}/${n1} ${n0}
expect 0 -u 65534 -g 65534 rmdir ../${n3}/${n1}

# Check whether user 65534 is permitted to create
# subdirectories, but not files - and to remove neither of them.
expect 0 prependacl . user:65534:write_data::deny,user:65534:append_data::allow

# Can create files?
expect EACCES -u 65534 -g 65534 create ${n0} 0644

# Can create symlinks?
expect 0 create ${n0} 0644
expect EACCES -u 65534 -g 65534 link ${n0} ${n1}
expect ENOENT -u 65534 -g 65534 unlink ${n1}
expect EACCES -u 65534 -g 65534 unlink ${n0}
expect 0 unlink ${n0}

# Can create directories?
expect 0 -u 65534 -g 65534 mkdir ${n0} 0755
expect EACCES -u 65534 -g 65534 rmdir ${n0}
expect 0 rmdir ${n0}

# Can move files from other directory?
expect 0 create ../${n3}/${n1} 0644
expect EACCES -u 65534 -g 65534 rename ../${n3}/${n1} ${n0}

# Can move files from other directory overwriting existing files?
expect EACCES -u 65534 -g 65534 rename ../${n3}/${n1} ${n0}
expect 0 -u 65534 -g 65534 unlink ../${n3}/${n1}

# Can move directories from other directory?
expect 0 mkdir ../${n3}/${n1} 0777
expect 0 -u 65534 -g 65534 rename ../${n3}/${n1} ${n0}

# Can move directories from other directory overwriting existing directory?
expect 0 mkdir ../${n3}/${n1} 0777
expect EACCES -u 65534 -g 65534 rename ../${n3}/${n1} ${n0}
expect 0 prependacl . user:65534:delete_child::allow
expect 0 -u 65534 -g 65534 rename ../${n3}/${n1} ${n0}
expect 0 -u 65534 -g 65534 rmdir ${n0}

cd ${cdir}
expect 0 rmdir ${n2}
expect 0 rmdir ${n3}
