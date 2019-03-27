#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chmod/08.t 211352 2010-08-15 21:24:17Z pjd $

desc="chmod returns EPERM if the named file has its immutable or append-only flag set"

dir=`dirname $0`
. ${dir}/../misc.sh

require chflags

case "${os}:${fs}" in
FreeBSD:ZFS)
	echo "1..29"
	;;
FreeBSD:UFS)
	echo "1..54"
	;;
*)
	quick_exit
esac

n0=`namegen`

expect 0 create ${n0} 0644
expect 0 chflags ${n0} SF_IMMUTABLE
expect EPERM chmod ${n0} 0600
supported lchmod && expect EPERM lchmod ${n0} 0600
expect 0644 stat ${n0} mode
expect 0 chflags ${n0} none
expect 0 chmod ${n0} 0600
expect 0600 stat ${n0} mode
supported lchmod && expect 0 lchmod ${n0} 0400
supported lchmod && expect 0400 stat ${n0} mode
expect 0 unlink ${n0}

expect 0 create ${n0} 0644
expect 0 chflags ${n0} SF_NOUNLINK
expect 0 chmod ${n0} 0600
expect 0600 stat ${n0} mode
supported lchmod && expect 0 lchmod ${n0} 0400
supported lchmod && expect 0400 stat ${n0} mode
expect 0 chflags ${n0} none
expect 0 unlink ${n0}

case "${os}:${fs}" in
FreeBSD:ZFS)
	expect 0 create ${n0} 0644
	expect 0 chflags ${n0} SF_APPEND
	expect 0 chmod ${n0} 0600
	expect 0600 stat ${n0} mode
	supported lchmod && expect 0 lchmod ${n0} 0500
	supported lchmod && expect 0500 stat ${n0} mode
	expect 0 chflags ${n0} none
	expect 0 chmod ${n0} 0400
	expect 0400 stat ${n0} mode
	expect 0 unlink ${n0}
	;;
FreeBSD:UFS)
	expect 0 create ${n0} 0644
	expect 0 chflags ${n0} SF_APPEND
	expect EPERM chmod ${n0} 0600
	supported lchmod && expect EPERM lchmod ${n0} 0600
	expect 0644 stat ${n0} mode
	expect 0 chflags ${n0} none
	expect 0 chmod ${n0} 0600
	expect 0600 stat ${n0} mode
	expect 0 unlink ${n0}

	expect 0 create ${n0} 0644
	expect 0 chflags ${n0} UF_IMMUTABLE
	expect EPERM chmod ${n0} 0600
	supported lchmod && expect EPERM lchmod ${n0} 0600
	expect 0644 stat ${n0} mode
	expect 0 chflags ${n0} none
	expect 0 chmod ${n0} 0600
	expect 0600 stat ${n0} mode
	expect 0 unlink ${n0}

	expect 0 create ${n0} 0644
	expect 0 chflags ${n0} UF_NOUNLINK
	expect 0 chmod ${n0} 0600
	expect 0600 stat ${n0} mode
	supported lchmod && expect 0 lchmod ${n0} 0400
	supported lchmod && expect 0400 stat ${n0} mode
	expect 0 chflags ${n0} none
	expect 0 unlink ${n0}

	expect 0 create ${n0} 0644
	expect 0 chflags ${n0} UF_APPEND
	expect EPERM chmod ${n0} 0600
	supported lchmod && expect EPERM lchmod ${n0} 0600
	expect 0644 stat ${n0} mode
	expect 0 chflags ${n0} none
	expect 0 chmod ${n0} 0600
	expect 0600 stat ${n0} mode
	expect 0 unlink ${n0}
	;;
esac
