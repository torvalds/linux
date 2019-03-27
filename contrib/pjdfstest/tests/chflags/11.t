#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chflags/11.t 211352 2010-08-15 21:24:17Z pjd $

desc="chflags returns EPERM if a user tries to set or remove the SF_SNAPSHOT flag"

dir=`dirname $0`
. ${dir}/../misc.sh

require chflags_SF_SNAPSHOT

echo "1..145"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
cdir=`pwd`
cd ${n0}

for type in regular dir fifo block char socket symlink; do
	if [ "${type}" != "symlink" ]; then
		create_file ${type} ${n1}
		expect EPERM -u 65534 -g 65534 chflags ${n1} SF_SNAPSHOT
		expect none stat ${n1} flags
		expect EPERM chflags ${n1} SF_SNAPSHOT
		expect none stat ${n1} flags
		expect 0 chown ${n1} 65534 65534
		expect EPERM -u 65534 -g 65534 chflags ${n1} SF_SNAPSHOT
		expect none stat ${n1} flags
		expect EPERM chflags ${n1} SF_SNAPSHOT
		expect none stat ${n1} flags
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n1}
		else
			expect 0 unlink ${n1}
		fi
	fi

	create_file ${type} ${n1}
	expect EPERM -u 65534 -g 65534 lchflags ${n1} SF_SNAPSHOT
	expect none lstat ${n1} flags
	expect EPERM lchflags ${n1} SF_SNAPSHOT
	expect none lstat ${n1} flags
	expect 0 lchown ${n1} 65534 65534
	expect EPERM -u 65534 -g 65534 lchflags ${n1} SF_SNAPSHOT
	expect none lstat ${n1} flags
	expect EPERM lchflags ${n1} SF_SNAPSHOT
	expect none lstat ${n1} flags
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n1}
	else
		expect 0 unlink ${n1}
	fi
done

cd ${cdir}
expect 0 rmdir ${n0}
