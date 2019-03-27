#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chflags/08.t 211352 2010-08-15 21:24:17Z pjd $

desc="chflags returns EPERM when one of SF_IMMUTABLE, SF_APPEND, or SF_NOUNLINK is set and the user is not the super-user"

dir=`dirname $0`
. ${dir}/../misc.sh

require chflags

echo "1..249"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
cdir=`pwd`
cd ${n0}

for type in regular dir fifo block char socket symlink; do
	if [ "${type}" != "symlink" ]; then
		create_file ${type} ${n1}
		expect 0 chown ${n1} 65534 65534
		for flag in SF_IMMUTABLE SF_APPEND SF_NOUNLINK; do
			expect 0 chflags ${n1} ${flag}
			expect EPERM -u 65533 -g 65533 chflags ${n1} UF_NODUMP
			expect ${flag} stat ${n1} flags
			expect EPERM -u 65534 -g 65534 chflags ${n1} UF_NODUMP
			expect ${flag} stat ${n1} flags
		done
		expect 0 chflags ${n1} none
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n1}
		else
			expect 0 unlink ${n1}
		fi
	fi

	create_file ${type} ${n1}
	expect 0 lchown ${n1} 65534 65534
	for flag in SF_IMMUTABLE SF_APPEND SF_NOUNLINK; do
		expect 0 lchflags ${n1} ${flag}
		expect EPERM -u 65533 -g 65533 lchflags ${n1} UF_NODUMP
		expect ${flag} lstat ${n1} flags
		expect EPERM -u 65534 -g 65534 lchflags ${n1} UF_NODUMP
		expect ${flag} lstat ${n1} flags
	done
	expect 0 lchflags ${n1} none
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n1}
	else
		expect 0 unlink ${n1}
	fi
done

cd ${cdir}
expect 0 rmdir ${n0}
