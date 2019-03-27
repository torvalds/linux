#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/granular/03.t 211352 2010-08-15 21:24:17Z pjd $

desc="NFSv4 granular permissions checking - DELETE and DELETE_CHILD"

dir=`dirname $0`
. ${dir}/../misc.sh

[ "${os}:${fs}" = "FreeBSD:ZFS" ] || quick_exit

echo "1..65"

n0=`namegen`
n1=`namegen`
n2=`namegen`
n3=`namegen`

expect 0 mkdir ${n2} 0755
expect 0 mkdir ${n3} 0777
cdir=`pwd`
cd ${n2}

# Unlink allowed on writable directory.
expect 0 create ${n0} 0644
expect EACCES -u 65534 -g 65534 unlink ${n0}
expect 0 prependacl . user:65534:write_data::allow
expect 0 -u 65534 -g 65534 unlink ${n0}

# Moving file elsewhere allowed on writable directory.
expect 0 create ${n0} 0644
expect 0 prependacl . user:65534:write_data::deny
expect EACCES -u 65534 -g 65534 rename ${n0} ../${n3}/${n0}
expect 0 prependacl . user:65534:write_data::allow
expect 0 -u 65534 -g 65534 rename ${n0} ../${n3}/${n0}

# Moving file from elsewhere allowed on writable directory.
expect 0 -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 -u 65534 -g 65534 unlink ${n0}

# Moving file from elsewhere overwriting local file allowed
# on writable directory.
expect 0 create ${n0} 0644
expect 0 create ../${n3}/${n0} 0644
expect 0 prependacl . user:65534:write_data::deny
expect EACCES -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 prependacl . user:65534:write_data::allow
expect 0 -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 -u 65534 -g 65534 unlink ${n0}

# Denied DELETE changes nothing wrt removing.
expect 0 create ${n0} 0644
expect 0 prependacl ${n0} user:65534:delete::deny
expect 0 -u 65534 -g 65534 unlink ${n0}

# Denied DELETE changes nothing wrt moving elsewhere or from elsewhere.
expect 0 create ${n0} 0644
expect 0 -u 65534 -g 65534 rename ${n0} ../${n3}/${n0}
expect 0 -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 -u 65534 -g 65534 unlink ${n0}

# DELETE_CHILD denies unlink on writable directory.
expect 0 create ${n0} 0644
expect 0 prependacl . user:65534:delete_child::deny
expect EPERM -u 65534 -g 65534 unlink ${n0}
expect 0 unlink ${n0}

# DELETE_CHILD denies moving file elsewhere.
expect 0 create ${n0} 0644
expect EPERM -u 65534 -g 65534 rename ${n0} ../${n3}/${n0}
expect 0 rename ${n0} ../${n3}/${n0}

# DELETE_CHILD does not deny moving file from elsewhere
# to a writable directory.
expect 0 -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}

# DELETE_CHILD denies moving file from elsewhere
# to a writable directory overwriting local file.
expect 0 create ../${n3}/${n0} 0644
expect EPERM -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}

# DELETE allowed on file allows for unlinking, no matter
# what permissions on containing directory are.
expect 0 prependacl ${n0} user:65534:delete::allow
expect 0 -u 65534 -g 65534 unlink ${n0}

# Same for moving the file elsewhere.
expect 0 create ${n0} 0644
expect 0 prependacl ${n0} user:65534:delete::allow
expect 0 -u 65534 -g 65534 rename ${n0} ../${n3}/${n0}

# Same for moving the file from elsewhere into a writable
# directory with DELETE_CHILD denied.
expect 0 -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 unlink ${n0}

# DELETE does not allow for overwriting a file in a unwritable
# directory with DELETE_CHILD denied.
expect 0 create ${n0} 0644
expect 0 create ../${n3}/${n0} 0644
expect 0 prependacl . user:65534:write_data::deny
expect 0 prependacl . user:65534:delete_child::deny
expect EPERM -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}
expect 0 prependacl ${n0} user:65534:delete::allow
expect EACCES -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}

# But it allows for plain deletion.
expect 0 -u 65534 -g 65534 unlink ${n0}

# DELETE_CHILD allowed on unwritable directory.
expect 0 create ${n0} 0644
expect 0 prependacl . user:65534:delete_child::allow
expect 0 -u 65534 -g 65534 unlink ${n0}

# Moving things elsewhere is allowed.
expect 0 create ${n0} 0644
expect 0 -u 65534 -g 65534 rename ${n0} ../${n3}/${n0}

# Moving things back is not.
expect EACCES -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}

# Even if we're overwriting.
expect 0 create ${n0} 0644
expect EACCES -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}

# Even if we have DELETE on the existing file.
expect 0 prependacl ${n0} user:65534:delete::allow
expect EACCES -u 65534 -g 65534 rename ../${n3}/${n0} ${n0}

# Denied DELETE changes nothing wrt removing.
expect 0 prependacl ${n0} user:65534:delete::deny
expect 0 -u 65534 -g 65534 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n2}
