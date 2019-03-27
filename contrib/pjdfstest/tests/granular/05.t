#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/granular/05.t 211352 2010-08-15 21:24:17Z pjd $

desc="NFSv4 granular permissions checking - DELETE and DELETE_CHILD with directories"

dir=`dirname $0`
. ${dir}/../misc.sh

[ "${os}:${fs}" = "FreeBSD:ZFS" ] || quick_exit

echo "1..68"

n0=`namegen`
n1=`namegen`
n2=`namegen`
n3=`namegen`

expect 0 mkdir ${n2} 0755
expect 0 mkdir ${n3} 0777
cdir=`pwd`
cd ${n2}

# Unlink allowed on writable directory.
expect 0 mkdir ${n0} 0755
expect EACCES -u 65534 -g 65534 rmdir ${n0}
expect 0 prependacl . user:65534:write_data::allow
expect 0 -u 65534 -g 65534 rmdir ${n0}

# Moving directory elsewhere allowed on writable directory.
expect 0 mkdir ${n0} 0777
expect 0 prependacl . user:65534:write_data::deny
expect EACCES -u 65534 -g 65534 rename ${n0} ../${n3}/${n0}
expect 0 prependacl . user:65534:write_data::allow
expect 0 -u 65534 -g 65534 rename ${n0} ../${n3}/${n0}

# 12
# Moving directory from elsewhere allowed on writable directory.
expect EACCES -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 prependacl . user:65534:append_data::allow
expect 0 -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 -u 65534 -g 65534 rmdir ${n0}

# Moving directory from elsewhere overwriting local directory allowed
# on writable directory.
expect 0 mkdir ${n0} 0755
expect 0 mkdir ../${n3}/${n0} 0777
expect 0 prependacl . user:65534:write_data::deny
expect EACCES -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 prependacl . user:65534:write_data::allow
expect 0 -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 -u 65534 -g 65534 rmdir ${n0}

# 23
# Denied DELETE changes nothing wrt removing.
expect 0 mkdir ${n0} 0755
expect 0 prependacl ${n0} user:65534:delete::deny
expect 0 -u 65534 -g 65534 rmdir ${n0}

# Denied DELETE changes nothing wrt moving elsewhere or from elsewhere.
expect 0 mkdir ${n0} 0777
expect 0 -u 65534 -g 65534 rename ${n0} ../${n3}/${n0}
expect 0 -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 -u 65534 -g 65534 rmdir ${n0}

# DELETE_CHILD denies unlink on writable directory.
expect 0 mkdir ${n0} 0755
expect 0 prependacl . user:65534:delete_child::deny
expect EPERM -u 65534 -g 65534 rmdir ${n0}
expect 0 rmdir ${n0}

# 35
# DELETE_CHILD denies moving directory elsewhere.
expect 0 mkdir ${n0} 0777
expect EPERM -u 65534 -g 65534 rename ${n0} ../${n3}/${n0}
expect 0 rename ${n0} ../${n3}/${n0}

# DELETE_CHILD does not deny moving directory from elsewhere
# to a writable directory.
expect 0 -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}

# DELETE_CHILD denies moving directory from elsewhere
# to a writable directory overwriting local directory.
expect 0 mkdir ../${n3}/${n0} 0755
expect EACCES -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}

# DELETE allowed on directory allows for unlinking, no matter
# what permissions on containing directory are.
expect 0 prependacl ${n0} user:65534:delete::allow
expect 0 -u 65534 -g 65534 rmdir ${n0}

# Same for moving the directory elsewhere.
expect 0 mkdir ${n0} 0777
expect 0 prependacl ${n0} user:65534:delete::allow
expect 0 -u 65534 -g 65534 rename ${n0} ../${n3}/${n0}

# 46
# Same for moving the directory from elsewhere into a writable
# directory with DELETE_CHILD denied.
expect 0 -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 rmdir ${n0}

# DELETE does not allow for overwriting a directory in a unwritable
# directory with DELETE_CHILD denied.
expect 0 mkdir ${n0} 0755
expect 0 mkdir ../${n3}/${n0} 0777
expect 0 prependacl . user:65534:write_data::deny
expect 0 prependacl . user:65534:delete_child::deny
expect EPERM -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 prependacl ${n0} user:65534:delete::allow
# XXX: expect EACCES -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}

# 54
# But it allows for plain deletion.
# XXX: expect 0 -u 65534 -g 65534 rmdir ${n0}
expect 0 rmdir ${n0}

# DELETE_CHILD allowed on unwritable directory.
expect 0 mkdir ${n0} 0755
expect 0 prependacl . user:65534:delete_child::allow
expect 0 -u 65534 -g 65534 rmdir ${n0}

# Moving things elsewhere is allowed.
expect 0 mkdir ${n0} 0777
expect 0 -u 65534 -g 65534 rename ${n0} ../${n3}/${n0}

# 60
# Moving things back is not.
# XXX: expect EACCES -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}

# Even if we're overwriting.
# XXX: expect 0 mkdir ${n0} 0755
expect 0 mkdir ../${n3}/${n0} 0777
# XXX: expect EACCES -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 mkdir ../${n3}/${n0} 0777

# Even if we have DELETE on the existing directory.
expect 0 prependacl ${n0} user:65534:delete::allow
# XXX: expect EACCES -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}

# Denied DELETE changes nothing wrt removing.
expect 0 prependacl ${n0} user:65534:delete::deny
expect 0 -u 65534 -g 65534 rmdir ${n0}

cd ${cdir}
expect 0 rmdir ${n2}
