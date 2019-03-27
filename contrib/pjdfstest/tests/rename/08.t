#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/rename/08.t 211352 2010-08-15 21:24:17Z pjd $

desc="rename returns EPERM if the parent directory of the file pointed at by the 'to' argument has its immutable flag set"

dir=`dirname $0`
. ${dir}/../misc.sh

require chflags

case "${os}:${fs}" in
FreeBSD:ZFS)
	flags1="SF_IMMUTABLE"
	flags2="SF_NOUNLINK SF_APPEND"
	echo "1..128"
	;;
FreeBSD:UFS)
	flags1="SF_IMMUTABLE UF_IMMUTABLE"
	flags2="SF_NOUNLINK SF_APPEND UF_NOUNLINK UF_APPEND"
	echo "1..219"
	;;
*)
	quick_exit
esac

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755

for type in regular dir fifo block char socket symlink; do
	create_file ${type} ${n1}
	for flag in ${flags1}; do
		expect 0 chflags ${n0} ${flag}
		expect ${flag} stat ${n0} flags
		expect EPERM rename ${n1} ${n0}/${n2}
	done
	expect 0 chflags ${n0} none
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n1}
	else
		expect 0 unlink ${n1}
	fi
done

for type in regular dir fifo block char socket symlink; do
	create_file ${type} ${n1}
	for flag in ${flags2}; do
		expect 0 chflags ${n0} ${flag}
		expect ${flag} stat ${n0} flags
		expect 0 rename ${n1} ${n0}/${n2}
		expect 0 chflags ${n0} none
		expect 0 rename ${n0}/${n2} ${n1}
	done
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n1}
	else
		expect 0 unlink ${n1}
	fi
done

expect 0 rmdir ${n0}
