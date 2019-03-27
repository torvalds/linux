#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/rename/07.t 211352 2010-08-15 21:24:17Z pjd $

desc="rename returns EPERM if the parent directory of the file pointed at by the 'from' argument has its immutable or append-only flag set"

dir=`dirname $0`
. ${dir}/../misc.sh

require chflags

case "${os}:${fs}" in
FreeBSD:ZFS)
	flags1="SF_IMMUTABLE SF_APPEND"
	flags2="SF_NOUNLINK"
	echo "1..128"
	;;
FreeBSD:UFS)
	flags1="SF_IMMUTABLE SF_APPEND UF_IMMUTABLE UF_APPEND"
	flags2="SF_NOUNLINK UF_NOUNLINK"
	echo "1..212"
	;;
*)
	quick_exit
esac

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755

for type in regular dir fifo block char socket symlink; do
	create_file ${type} ${n0}/${n1}
	for flag in ${flags1}; do
		expect 0 chflags ${n0} ${flag}
		expect ${flag} stat ${n0} flags
		[ "${flag}" = "SF_APPEND" ] && todo FreeBSD:ZFS "Renaming a file protected by SF_APPEND should return EPERM."
		expect EPERM rename ${n0}/${n1} ${n2}
		[ "${flag}" = "SF_APPEND" ] && todo FreeBSD:ZFS "Renaming a file protected by SF_APPEND should return EPERM."
		expect ENOENT rename ${n2} ${n0}/${n1}
	done
	expect 0 chflags ${n0} none
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n0}/${n1}
	else
		expect 0 unlink ${n0}/${n1}
	fi
done

for type in regular dir fifo block char socket symlink; do
	create_file ${type} ${n0}/${n1}
	for flag in ${flags2}; do
		expect 0 chflags ${n0} ${flag}
		expect ${flag} stat ${n0} flags
		expect 0 rename ${n0}/${n1} ${n2}
		expect 0 rename ${n2} ${n0}/${n1}
	done
	expect 0 chflags ${n0} none
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n0}/${n1}
	else
		expect 0 unlink ${n0}/${n1}
	fi
done

expect 0 rmdir ${n0}
