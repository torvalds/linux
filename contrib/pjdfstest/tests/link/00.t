#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/link/00.t 211352 2010-08-15 21:24:17Z pjd $

desc="link creates hardlinks"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..202"

n0=`namegen`
n1=`namegen`
n2=`namegen`
n3=`namegen`

expect 0 mkdir ${n3} 0755
cdir=`pwd`
cd ${n3}

for type in regular fifo block char socket; do
	create_file ${type} ${n0}
	expect ${type},1 lstat ${n0} type,nlink

	expect 0 link ${n0} ${n1}
	expect ${type},2 lstat ${n0} type,nlink
	expect ${type},2 lstat ${n1} type,nlink

	expect 0 link ${n1} ${n2}
	expect ${type},3 lstat ${n0} type,nlink
	expect ${type},3 lstat ${n1} type,nlink
	expect ${type},3 lstat ${n2} type,nlink

	expect 0 chmod ${n1} 0201
	expect 0 chown ${n1} 65534 65533

	expect ${type},0201,3,65534,65533 lstat ${n0} type,mode,nlink,uid,gid
	expect ${type},0201,3,65534,65533 lstat ${n1} type,mode,nlink,uid,gid
	expect ${type},0201,3,65534,65533 lstat ${n2} type,mode,nlink,uid,gid

	expect 0 unlink ${n0}
	expect ENOENT lstat ${n0} type,mode,nlink,uid,gid
	expect ${type},0201,2,65534,65533 lstat ${n1} type,mode,nlink,uid,gid
	expect ${type},0201,2,65534,65533 lstat ${n2} type,mode,nlink,uid,gid

	expect 0 unlink ${n2}
	expect ENOENT lstat ${n0} type,mode,nlink,uid,gid
	expect ${type},0201,1,65534,65533 lstat ${n1} type,mode,nlink,uid,gid
	expect ENOENT lstat ${n2} type,mode,nlink,uid,gid

	expect 0 unlink ${n1}
	expect ENOENT lstat ${n0} type,mode,nlink,uid,gid
	expect ENOENT lstat ${n1} type,mode,nlink,uid,gid
	expect ENOENT lstat ${n2} type,mode,nlink,uid,gid
done

# successful link(2) updates ctime.
for type in regular fifo block char socket; do
	create_file ${type} ${n0}
	ctime1=`${fstest} stat ${n0} ctime`
	dctime1=`${fstest} stat . ctime`
	dmtime1=`${fstest} stat . mtime`
	sleep 1
	expect 0 link ${n0} ${n1}
	ctime2=`${fstest} stat ${n0} ctime`
	test_check $ctime1 -lt $ctime2
	dctime2=`${fstest} stat . ctime`
	test_check $dctime1 -lt $dctime2
	dmtime2=`${fstest} stat . mtime`
	test_check $dctime1 -lt $dmtime2
	expect 0 unlink ${n0}
	expect 0 unlink ${n1}
done

# unsuccessful link(2) does not update ctime.
for type in regular fifo block char socket; do
	create_file ${type} ${n0}
	expect 0 -- chown ${n0} 65534 -1
	ctime1=`${fstest} stat ${n0} ctime`
	dctime1=`${fstest} stat . ctime`
	dmtime1=`${fstest} stat . mtime`
	sleep 1
	expect EACCES -u 65534 link ${n0} ${n1}
	ctime2=`${fstest} stat ${n0} ctime`
	test_check $ctime1 -eq $ctime2
	dctime2=`${fstest} stat . ctime`
	test_check $dctime1 -eq $dctime2
	dmtime2=`${fstest} stat . mtime`
	test_check $dctime1 -eq $dmtime2
	expect 0 unlink ${n0}
done

cd ${cdir}
expect 0 rmdir ${n3}
