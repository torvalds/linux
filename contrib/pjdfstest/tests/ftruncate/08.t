#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/truncate/08.t 211352 2010-08-15 21:24:17Z pjd $

desc="truncate returns EPERM if the named file has its immutable or append-only flag set"

dir=`dirname $0`
. ${dir}/../misc.sh

require chflags

case "${os}:${fs}" in
FreeBSD:ZFS)
	echo "1..22"
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
expect EPERM truncate ${n0} 123
expect 0 stat ${n0} size
expect 0 chflags ${n0} none
expect 0 truncate ${n0} 123
expect 123 stat ${n0} size
expect 0 unlink ${n0}

expect 0 create ${n0} 0644
expect 0 chflags ${n0} SF_NOUNLINK
expect 0 truncate ${n0} 123
expect 123 stat ${n0} size
expect 0 chflags ${n0} none
expect 0 unlink ${n0}

expect 0 create ${n0} 0644
expect 0 chflags ${n0} SF_APPEND
todo FreeBSD:ZFS "Truncating a file protected by SF_APPEND should return EPERM."
expect EPERM truncate ${n0} 123
todo FreeBSD:ZFS "Truncating a file protected by SF_APPEND should return EPERM."
expect 0 stat ${n0} size
expect 0 chflags ${n0} none
expect 0 truncate ${n0} 123
expect 123 stat ${n0} size
expect 0 unlink ${n0}

case "${os}:${fs}" in
FreeBSD:UFS)
	expect 0 create ${n0} 0644
	expect 0 chflags ${n0} UF_IMMUTABLE
	expect EPERM truncate ${n0} 123
	expect 0 stat ${n0} size
	expect 0 chflags ${n0} none
	expect 0 truncate ${n0} 123
	expect 123 stat ${n0} size
	expect 0 unlink ${n0}

	expect 0 create ${n0} 0644
	expect 0 chflags ${n0} UF_NOUNLINK
	expect 0 truncate ${n0} 123
	expect 123 stat ${n0} size
	expect 0 chflags ${n0} none
	expect 0 unlink ${n0}

	expect 0 create ${n0} 0644
	expect 0 chflags ${n0} UF_APPEND
	expect EPERM truncate ${n0} 123
	expect 0 stat ${n0} size
	expect 0 chflags ${n0} none
	expect 0 truncate ${n0} 123
	expect 123 stat ${n0} size
	expect 0 unlink ${n0}
	;;
esac
