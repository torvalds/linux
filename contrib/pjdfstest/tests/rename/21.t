#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/rename/21.t 211352 2010-08-15 21:24:17Z pjd $

desc="write access to subdirectory is required to move it to another directory"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..16"

n0=`namegen`
n1=`namegen`
n2=`namegen`
n3=`namegen`

expect 0 mkdir ${n2} 0777
expect 0 mkdir ${n3} 0777
cdir=`pwd`

# Check that write permission on containing directory (${n2}) is enough
# to rename subdirectory (${n0}). If we rename directory write access
# to this directory may also be required.
expect 0 mkdir ${n2}/${n0} 0700
expect "0|EACCES" -u 65534 -g 65534 rename ${n2}/${n0} ${n2}/${n1}
expect "0|EACCES" -u 65534 -g 65534 rename ${n2}/${n1} ${n2}/${n0}

# Check that write permission on containing directory (${n2}) is not enough
# to move subdirectory (${n0}) from that directory.
# Actually POSIX says that write access to ${n2} and ${n3} may be enough
# to move ${n0} from ${n2} to ${n3}.
expect "0|EACCES" -u 65534 -g 65534 rename ${n2}/${n0} ${n3}/${n1}

expect "0|ENOENT" rmdir ${n2}/${n0}
expect ENOENT rmdir ${n2}/${n0}
expect "0|ENOENT" rmdir ${n3}/${n1}
expect ENOENT rmdir ${n3}/${n1}

# Check that write permission on containing directory (${n2}) is enough
# to move file (${n0}) from that directory.
expect 0 create ${n2}/${n0} 0644
expect 0 -u 65534 -g 65534 rename ${n2}/${n0} ${n3}/${n1}

expect 0 unlink ${n3}/${n1}
expect ENOENT unlink ${n2}/${n0}

expect 0 rmdir ${n3}
expect 0 rmdir ${n2}
