#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/link/12.t 211352 2010-08-15 21:24:17Z pjd $

desc="link returns EPERM if the source file has its immutable or append-only flag set"

dir=`dirname $0`
. ${dir}/../misc.sh

require chflags

case "${os}:${fs}" in
FreeBSD:ZFS)
	echo "1..28"
	;;
FreeBSD:UFS)
	echo "1..48"
	;;
*)
	quick_exit
esac

n0=`namegen`
n1=`namegen`

expect 0 create ${n0} 0644

expect 1 stat ${n0} nlink
expect 0 link ${n0} ${n1}
expect 2 stat ${n0} nlink
expect 0 unlink ${n1}
expect 1 stat ${n0} nlink

expect 0 chflags ${n0} SF_IMMUTABLE
expect EPERM link ${n0} ${n1}
expect 1 stat ${n0} nlink
expect 0 chflags ${n0} none
expect 0 link ${n0} ${n1}
expect 2 stat ${n0} nlink
expect 0 unlink ${n1}
expect 1 stat ${n0} nlink

expect 0 chflags ${n0} SF_NOUNLINK
expect 0 link ${n0} ${n1}
expect 2 stat ${n0} nlink
expect 0 chflags ${n0} none
expect 0 unlink ${n1}
expect 1 stat ${n0} nlink

expect 0 chflags ${n0} SF_APPEND
expect EPERM link ${n0} ${n1}
expect 0 chflags ${n0} none
expect 0 link ${n0} ${n1}
expect 2 stat ${n0} nlink
expect 0 unlink ${n1}
expect 1 stat ${n0} nlink

case "${os}:${fs}" in
FreeBSD:UFS)
	expect 0 chflags ${n0} UF_IMMUTABLE
	expect EPERM link ${n0} ${n1}
	expect 0 chflags ${n0} none
	expect 0 link ${n0} ${n1}
	expect 2 stat ${n0} nlink
	expect 0 unlink ${n1}
	expect 1 stat ${n0} nlink

	expect 0 chflags ${n0} UF_NOUNLINK
	expect 0 link ${n0} ${n1}
	expect 2 stat ${n0} nlink
	expect 0 chflags ${n0} none
	expect 0 unlink ${n1}
	expect 1 stat ${n0} nlink

	expect 0 chflags ${n0} UF_APPEND
	expect EPERM link ${n0} ${n1}
	expect 0 chflags ${n0} none
	expect 0 link ${n0} ${n1}
	expect 2 stat ${n0} nlink
	expect 0 unlink ${n1}
	expect 1 stat ${n0} nlink
	;;
esac

expect 0 unlink ${n0}
