#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/unlink/11.t 211352 2010-08-15 21:24:17Z pjd $

desc="unlink returns EACCES or EPERM if the directory containing the file is marked sticky, and neither the containing directory nor the file to be removed are owned by the effective user ID"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..270"

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n2} 0755
cdir=`pwd`
cd ${n2}

expect 0 mkdir ${n0} 0755
expect 0 chmod ${n0} 01777
expect 0 chown ${n0} 65534 65534

for type in regular fifo block char socket symlink; do
	# User owns both: the sticky directory and the file.
	expect 0 chown ${n0} 65534 65534
	create_file ${type} ${n0}/${n1} 65534 65534
	expect ${type},65534,65534 lstat ${n0}/${n1} type,uid,gid
	expect 0 -u 65534 -g 65534 unlink ${n0}/${n1}
	expect ENOENT lstat ${n0}/${n1} type

	# User owns the sticky directory, but doesn't own the file.
	for id in 0 65533; do
		expect 0 chown ${n0} 65534 65534
		create_file ${type} ${n0}/${n1} ${id} ${id}
		expect ${type},${id},${id} lstat ${n0}/${n1} type,uid,gid
		expect 0 -u 65534 -g 65534 unlink ${n0}/${n1}
		expect ENOENT lstat ${n0}/${n1} type
	done

	# User owns the file, but doesn't own the sticky directory.
	for id in 0 65533; do
		expect 0 chown ${n0} ${id} ${id}
		create_file ${type} ${n0}/${n1} 65534 65534
		expect ${type},65534,65534 lstat ${n0}/${n1} type,uid,gid
		expect 0 -u 65534 -g 65534 unlink ${n0}/${n1}
		expect ENOENT lstat ${n0}/${n1} type
	done

	# User doesn't own the sticky directory nor the file.
	for id in 0 65533; do
		expect 0 chown ${n0} ${id} ${id}
		create_file ${type} ${n0}/${n1} ${id} ${id}
		expect ${type},${id},${id} lstat ${n0}/${n1} type,uid,gid
		expect "EACCES|EPERM" -u 65534 -g 65534 unlink ${n0}/${n1}
		expect ${type},${id},${id} lstat ${n0}/${n1} type,uid,gid
		expect 0 unlink ${n0}/${n1}
	done
done

expect 0 rmdir ${n0}

cd ${cdir}
expect 0 rmdir ${n2}
