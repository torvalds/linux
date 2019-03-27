#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/rename/00.t 211352 2010-08-15 21:24:17Z pjd $

desc="rename changes file name"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..150"

n0=`namegen`
n1=`namegen`
n2=`namegen`
n3=`namegen`

expect 0 mkdir ${n3} 0755
cdir=`pwd`
cd ${n3}

for type in regular fifo block char socket; do
	create_file ${type} ${n0} 0644
	expect ${type},0644,1 lstat ${n0} type,mode,nlink
	inode=`${fstest} lstat ${n0} inode`
	expect 0 rename ${n0} ${n1}
	expect ENOENT lstat ${n0} type,mode,nlink
	expect ${type},${inode},0644,1 lstat ${n1} type,inode,mode,nlink
	expect 0 link ${n1} ${n0}
	expect ${type},${inode},0644,2 lstat ${n0} type,inode,mode,nlink
	expect ${type},${inode},0644,2 lstat ${n1} type,inode,mode,nlink
	expect 0 rename ${n1} ${n2}
	expect ${type},${inode},0644,2 lstat ${n0} type,inode,mode,nlink
	expect ENOENT lstat ${n1} type,mode,nlink
	expect ${type},${inode},0644,2 lstat ${n2} type,inode,mode,nlink
	expect 0 unlink ${n0}
	expect 0 unlink ${n2}
done

expect 0 mkdir ${n0} 0755
expect dir,0755 lstat ${n0} type,mode
inode=`${fstest} lstat ${n0} inode`
expect 0 rename ${n0} ${n1}
expect ENOENT lstat ${n0} type,mode
expect dir,${inode},0755 lstat ${n1} type,inode,mode
expect 0 rmdir ${n1}

expect 0 create ${n0} 0644
rinode=`${fstest} lstat ${n0} inode`
expect regular,0644 lstat ${n0} type,mode
expect 0 symlink ${n0} ${n1}
sinode=`${fstest} lstat ${n1} inode`
expect regular,${rinode},0644 stat ${n1} type,inode,mode
expect symlink,${sinode} lstat ${n1} type,inode
expect 0 rename ${n1} ${n2}
expect regular,${rinode},0644 stat ${n0} type,inode,mode
expect ENOENT lstat ${n1} type,mode
expect symlink,${sinode} lstat ${n2} type,inode
expect 0 unlink ${n0}
expect 0 unlink ${n2}

# successful rename(2) updates ctime.
for type in regular dir fifo block char socket symlink; do
	create_file ${type} ${n0}
	ctime1=`${fstest} lstat ${n0} ctime`
	sleep 1
	expect 0 rename ${n0} ${n1}
	ctime2=`${fstest} lstat ${n1} ctime`
	test_check $ctime1 -lt $ctime2
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n1}
	else
		expect 0 unlink ${n1}
	fi
done

# unsuccessful link(2) does not update ctime.
for type in regular dir fifo block char socket symlink; do
	create_file ${type} ${n0}
	ctime1=`${fstest} lstat ${n0} ctime`
	sleep 1
	expect EACCES -u 65534 rename ${n0} ${n1}
	ctime2=`${fstest} lstat ${n0} ctime`
	test_check $ctime1 -eq $ctime2
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n0}
	else
		expect 0 unlink ${n0}
	fi
done

cd ${cdir}
expect 0 rmdir ${n3}
