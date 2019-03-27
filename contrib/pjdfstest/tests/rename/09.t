#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/rename/09.t 211352 2010-08-15 21:24:17Z pjd $

desc="rename returns EACCES or EPERM if the directory containing 'from' is marked sticky, and neither the containing directory nor 'from' are owned by the effective user ID"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..2353"

n0=`namegen`
n1=`namegen`
n2=`namegen`
n3=`namegen`
n4=`namegen`

expect 0 mkdir ${n4} 0755
cdir=`pwd`
cd ${n4}

expect 0 mkdir ${n0} 0755
expect 0 chmod ${n0} 01777
expect 0 chown ${n0} 65534 65534

expect 0 mkdir ${n1} 0755
expect 0 chown ${n1} 65534 65534

for type in regular fifo block char socket symlink; do
	# User owns both: the source sticky directory and the source file.
	expect 0 chown ${n0} 65534 65534
	create_file ${type} ${n0}/${n2} 65534 65534
	inode=`${fstest} lstat ${n0}/${n2} inode`

	for type in none regular fifo block char socket symlink; do
		create_file ${type} ${n1}/${n3} 65534 65534
		expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
		expect ENOENT lstat ${n0}/${n2} inode
		expect ${inode},65534,65534 lstat ${n1}/${n3} inode,uid,gid
		expect 0 -u 65534 -g 65534 rename ${n1}/${n3} ${n0}/${n2}
		expect ${inode} lstat ${n0}/${n2} inode
		expect ENOENT lstat ${n1}/${n3} inode
	done

	expect 0 unlink ${n0}/${n2}

	# User owns the source sticky directory, but doesn't own the source file.
	for id in 0 65533; do
		expect 0 chown ${n0} 65534 65534
		create_file ${type} ${n0}/${n2} ${id} ${id}
		inode=`${fstest} lstat ${n0}/${n2} inode`

		for type in none regular fifo block char socket symlink; do
			create_file ${type} ${n1}/${n3} 65534 65534
			expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
			expect ENOENT lstat ${n0}/${n2} inode
			expect ${inode},${id},${id} lstat ${n1}/${n3} inode,uid,gid
			expect 0 -u 65534 -g 65534 rename ${n1}/${n3} ${n0}/${n2}
			expect ${inode} lstat ${n0}/${n2} inode
			expect ENOENT lstat ${n1}/${n3} inode
		done

		expect 0 unlink ${n0}/${n2}
	done

	# User owns the source file, but doesn't own the source sticky directory.
	for id in 0 65533; do
		expect 0 chown ${n0} ${id} ${id}
		create_file ${type} ${n0}/${n2} 65534 65534
		inode=`${fstest} lstat ${n0}/${n2} inode`

		for type in none regular fifo block char socket symlink; do
			create_file ${type} ${n1}/${n3} 65534 65534
			expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
			expect ENOENT lstat ${n0}/${n2} inode
			expect ${inode},65534,65534 lstat ${n1}/${n3} inode,uid,gid
			expect 0 -u 65534 -g 65534 rename ${n1}/${n3} ${n0}/${n2}
			expect ${inode} lstat ${n0}/${n2} inode
			expect ENOENT lstat ${n1}/${n3} inode
		done

		expect 0 unlink ${n0}/${n2}
	done

	# User doesn't own the source sticky directory nor the source file.
	for id in 0 65533; do
		expect 0 chown ${n0} ${id} ${id}
		create_file ${type} ${n0}/${n2} ${id} ${id}
		inode=`${fstest} lstat ${n0}/${n2} inode`

		for type in none regular fifo block char socket symlink; do
			create_file ${type} ${n1}/${n3} 65534 65534
			expect "EACCES|EPERM" -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
			expect ${inode},${id},${id} lstat ${n0}/${n2} inode,uid,gid
			if [ "${type}" != "none" ]; then
				expect 65534,65534 lstat ${n1}/${n3} uid,gid
				expect 0 unlink ${n1}/${n3}
			fi
		done

		expect 0 unlink ${n0}/${n2}
	done
done

# User owns both: the source sticky directory and the source directory.
expect 0 chown ${n0} 65534 65534
create_file dir ${n0}/${n2} 65534 65534
inode=`${fstest} lstat ${n0}/${n2} inode`

expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} type
expect ${inode},65534,65534 lstat ${n1}/${n3} inode,uid,gid
expect 0 rename ${n1}/${n3} ${n0}/${n2}

expect 0 -u 65534 -g 65534 mkdir ${n1}/${n3} 0755
expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
expect ENOENT lstat ${n0}/${n2} type
expect ${inode},65534,65534 lstat ${n1}/${n3} inode,uid,gid
expect 0 rmdir ${n1}/${n3}

# User owns the source sticky directory, but doesn't own the source directory.
# This fails when changing parent directory, because this will modify
# source directory inode (the .. link in it), but we can still rename it
# without changing its parent directory.
for id in 0 65533; do
	expect 0 chown ${n0} 65534 65534
	create_file dir ${n0}/${n2} ${id} ${id}
	inode=`${fstest} lstat ${n0}/${n2} inode`

	expect "EACCES|EPERM" -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
	expect ${inode},${id},${id} lstat ${n0}/${n2} inode,uid,gid
	expect ENOENT lstat ${n1}/${n3} type

	expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n0}/${n3}
	expect ENOENT lstat ${n0}/${n2} type
	expect ${inode},${id},${id} lstat ${n0}/${n3} inode,uid,gid
	expect 0 rename ${n0}/${n3} ${n0}/${n2}

	expect 0 -u 65534 -g 65534 mkdir ${n1}/${n3} 0755
	expect "EACCES|EPERM" -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
	expect ${inode},${id},${id} lstat ${n0}/${n2} inode,uid,gid
	expect dir,${id},${id} lstat ${n0}/${n2} type,uid,gid
	expect 0 rmdir ${n1}/${n3}

	expect 0 -u 65534 -g 65534 mkdir ${n0}/${n3} 0755
	expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n0}/${n3}
	expect ENOENT lstat ${n0}/${n2} type
	expect ${inode},${id},${id} lstat ${n0}/${n3} inode,uid,gid
	expect 0 rmdir ${n0}/${n3}
done

# User owns the source directory, but doesn't own the source sticky directory.
for id in 0 65533; do
	expect 0 chown ${n0} ${id} ${id}
	create_file dir ${n0}/${n2} 65534 65534
	inode=`${fstest} lstat ${n0}/${n2} inode`

	expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
	expect ENOENT lstat ${n0}/${n2} type
	expect ${inode},65534,65534 lstat ${n1}/${n3} inode,uid,gid
	expect 0 rename ${n1}/${n3} ${n0}/${n2}

	expect 0 -u 65534 -g 65534 mkdir ${n1}/${n3} 0755
	expect 0 -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
	expect ENOENT lstat ${n0}/${n2} type
	expect ${inode},65534,65534 lstat ${n1}/${n3} inode,uid,gid
	expect 0 rmdir ${n1}/${n3}
done

# User doesn't own the source sticky directory nor the source directory.
for id in 0 65533; do
	expect 0 chown ${n0} ${id} ${id}
	create_file dir ${n0}/${n2} ${id} ${id}
	inode=`${fstest} lstat ${n0}/${n2} inode`

	expect "EACCES|EPERM" -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
	expect ${inode},${id},${id} lstat ${n0}/${n2} inode,uid,gid
	expect ENOENT lstat ${n1}/${n3} type

	expect 0 -u 65534 -g 65534 mkdir ${n1}/${n3} 0755
	expect "EACCES|EPERM" -u 65534 -g 65534 rename ${n0}/${n2} ${n1}/${n3}
	expect ${inode},${id},${id} lstat ${n0}/${n2} inode,uid,gid
	expect dir,65534,65534 lstat ${n1}/${n3} type,uid,gid
	expect 0 rmdir ${n0}/${n2}
	expect 0 rmdir ${n1}/${n3}
done

expect 0 rmdir ${n1}
expect 0 rmdir ${n0}

cd ${cdir}
expect 0 rmdir ${n4}
