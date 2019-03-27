#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/open/11.t 211352 2010-08-15 21:24:17Z pjd $

desc="open returns EPERM when the named file has its append-only flag set, the file is to be modified, and O_TRUNC is specified or O_APPEND is not specified"

dir=`dirname $0`
. ${dir}/../misc.sh

require chflags

case "${os}:${fs}" in
FreeBSD:ZFS)
	echo "1..12"
	;;
FreeBSD:UFS)
	echo "1..24"
	;;
*)
	quick_exit
esac

n0=`namegen`

expect 0 create ${n0} 0644
expect 0 chflags ${n0} SF_APPEND
expect 0 open ${n0} O_WRONLY,O_APPEND
expect 0 open ${n0} O_RDWR,O_APPEND
expect EPERM open ${n0} O_WRONLY
expect EPERM open ${n0} O_RDWR
todo FreeBSD:ZFS "When fle is protected by SF_APPEND, open(O_TRUNC) should return EPERM."
expect EPERM open ${n0} O_RDONLY,O_TRUNC
todo FreeBSD:ZFS "When fle is protected by SF_APPEND, open(O_TRUNC) should return EPERM."
expect EPERM open ${n0} O_RDONLY,O_APPEND,O_TRUNC
todo FreeBSD:ZFS "When fle is protected by SF_APPEND, open(O_TRUNC) should return EPERM."
expect EPERM open ${n0} O_WRONLY,O_APPEND,O_TRUNC
todo FreeBSD:ZFS "When fle is protected by SF_APPEND, open(O_TRUNC) should return EPERM."
expect EPERM open ${n0} O_RDWR,O_APPEND,O_TRUNC
expect 0 chflags ${n0} none
expect 0 unlink ${n0}

case "${os}:${fs}" in
FreeBSD:UFS)
	expect 0 create ${n0} 0644
	expect 0 chflags ${n0} UF_APPEND
	expect 0 open ${n0} O_WRONLY,O_APPEND
	expect 0 open ${n0} O_RDWR,O_APPEND
	expect EPERM open ${n0} O_WRONLY
	expect EPERM open ${n0} O_RDWR
	expect EPERM open ${n0} O_RDONLY,O_TRUNC
	expect EPERM open ${n0} O_RDONLY,O_APPEND,O_TRUNC
	expect EPERM open ${n0} O_WRONLY,O_APPEND,O_TRUNC
	expect EPERM open ${n0} O_RDWR,O_APPEND,O_TRUNC
	expect 0 chflags ${n0} none
	expect 0 unlink ${n0}
	;;
esac
