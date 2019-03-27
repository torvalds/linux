#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chown/08.t 211352 2010-08-15 21:24:17Z pjd $

desc="chown returns EPERM if the named file has its immutable or append-only flag set"

dir=`dirname $0`
. ${dir}/../misc.sh

require chflags

case "${os}:${fs}" in
FreeBSD:ZFS)
	echo "1..20"
	;;
FreeBSD:UFS)
	echo "1..44"
	;;
*)
	quick_exit
esac

n0=`namegen`

expect 0 create ${n0} 0644
expect 0 chflags ${n0} SF_IMMUTABLE
expect EPERM chown ${n0} 65534 65534
expect 0,0 stat ${n0} uid,gid
expect 0 chflags ${n0} none
expect 0 chown ${n0} 65534 65534
expect 65534,65534 stat ${n0} uid,gid
expect 0 unlink ${n0}

expect 0 create ${n0} 0644
expect 0 chflags ${n0} SF_NOUNLINK
expect 0 chown ${n0} 65534 65534
expect 65534,65534 stat ${n0} uid,gid
expect 0 chflags ${n0} none
expect 0 unlink ${n0}

case "${os}:${fs}" in
FreeBSD:ZFS)
	expect 0 create ${n0} 0644
	expect 0 chflags ${n0} SF_APPEND
	expect 0 chown ${n0} 65534 65534
	expect 65534,65534 stat ${n0} uid,gid
	expect 0 chflags ${n0} none
	expect 0 unlink ${n0}
	;;
FreeBSD:UFS)
	expect 0 create ${n0} 0644
	expect 0 chflags ${n0} SF_APPEND
	expect EPERM chown ${n0} 65534 65534
	expect 0,0 stat ${n0} uid,gid
	expect 0 chflags ${n0} none
	expect 0 chown ${n0} 65534 65534
	expect 65534,65534 stat ${n0} uid,gid
	expect 0 unlink ${n0}

	expect 0 create ${n0} 0644
	expect 0 chflags ${n0} UF_IMMUTABLE
	expect EPERM chown ${n0} 65534 65534
	expect 0,0 stat ${n0} uid,gid
	expect 0 chflags ${n0} none
	expect 0 chown ${n0} 65534 65534
	expect 65534,65534 stat ${n0} uid,gid
	expect 0 unlink ${n0}

	expect 0 create ${n0} 0644
	expect 0 chflags ${n0} UF_NOUNLINK
	expect 0 chown ${n0} 65534 65534
	expect 65534,65534 stat ${n0} uid,gid
	expect 0 chflags ${n0} none
	expect 0 unlink ${n0}

	expect 0 create ${n0} 0644
	expect 0 chflags ${n0} UF_APPEND
	expect EPERM chown ${n0} 65534 65534
	expect 0,0 stat ${n0} uid,gid
	expect 0 chflags ${n0} none
	expect 0 chown ${n0} 65534 65534
	expect 65534,65534 stat ${n0} uid,gid
	expect 0 unlink ${n0}
	;;
esac
