#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/rename/06.t 211352 2010-08-15 21:24:17Z pjd $

desc="rename returns EPERM if the file pointed at by the 'from' argument has its immutable, undeletable or append-only flag set"

dir=`dirname $0`
. ${dir}/../misc.sh

require chflags

case "${os}:${fs}" in
FreeBSD:ZFS)
	flags="SF_IMMUTABLE SF_NOUNLINK SF_APPEND"
	echo "1..195"
	;;
FreeBSD:UFS)
	flags="SF_IMMUTABLE SF_NOUNLINK SF_APPEND UF_IMMUTABLE UF_NOUNLINK UF_APPEND"
	echo "1..351"
	;;
*)
	quick_exit
esac

n0=`namegen`
n1=`namegen`

for type in regular dir fifo block char socket symlink; do
	if [ "${type}" != "symlink" ]; then
		create_file ${type} ${n0}
		for flag in ${flags}; do
			expect 0 chflags ${n0} ${flag}
			expect ${flag} stat ${n0} flags
			[ "${flag}" = "SF_APPEND" ] && todo FreeBSD:ZFS "Renaming a file protected by SF_APPEND should return EPERM."
			expect EPERM rename ${n0} ${n1}
			[ "${flag}" = "SF_APPEND" ] && todo FreeBSD:ZFS "Renaming a file protected by SF_APPEND should return EPERM."
			expect ENOENT rename ${n1} ${n0}
		done
		expect 0 chflags ${n0} none
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n0}
		else
			expect 0 unlink ${n0}
		fi
	fi

	create_file ${type} ${n0}
	for flag in ${flags}; do
		expect 0 lchflags ${n0} ${flag}
		expect ${flag} lstat ${n0} flags
		[ "${flag}" = "SF_APPEND" ] && todo FreeBSD:ZFS "Renaming a file protected by SF_APPEND should return EPERM."
		expect EPERM rename ${n0} ${n1}
		[ "${flag}" = "SF_APPEND" ] && todo FreeBSD:ZFS "Renaming a file protected by SF_APPEND should return EPERM."
		expect ENOENT rename ${n1} ${n0}
	done
	expect 0 lchflags ${n0} none
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n0}
	else
		expect 0 unlink ${n0}
	fi
done
