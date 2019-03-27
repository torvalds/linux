#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/mknod/11.t 211352 2010-08-15 21:24:17Z pjd $

desc="mknod creates device files"

dir=`dirname $0`
. ${dir}/../misc.sh

case "${os}" in
SunOS)
	echo "1..40"
        ;;
*)
	echo "1..28"
	;;
esac

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n1} 0755
cdir=`pwd`
cd ${n1}

for type in c b; do
	case "${type}" in
	c)
		stattype="char"
		;;
	b)
		stattype="block"
		;;
	esac

	# Create char special with old-style numbers
	expect 0 mknod ${n0} ${type} 0755 1 2
	expect ${stattype},0755 lstat ${n0} type,mode
	expect 1,2 lstat ${n0} major,minor
	expect EEXIST mknod ${n0} ${type} 0777 3 4
	expect 0 unlink ${n0}

	case "${os}" in
	SunOS)
		# Create char special with new-style numbers
		expect 0 mknod ${n0} ${type} 0755 4095 4095
		expect ${stattype},0755 lstat ${n0} type,mode
		expect 4095,4095 lstat ${n0} major,minor
		expect EEXIST mknod ${n0} ${type} 0777 4000 4000
		expect 0 unlink ${n0}

		# mknod returns EINVAL if device's numbers are too big
		# for 32-bit solaris !!
		expect EINVAL mknod ${n0} ${type} 0755 4096 262144
	        ;;
	esac

	# POSIX: Upon successful completion, mknod(2) shall mark for update the
	# st_atime, st_ctime, and st_mtime fields of the file. Also, the st_ctime and
	# st_mtime fields of the directory that contains the new entry shall be marked
	# for update.
	expect 0 chown . 0 0
	time=`${fstest} stat . ctime`
	sleep 1
	expect 0 mknod ${n0} ${type} 0755 1 2
	atime=`${fstest} stat ${n0} atime`
	test_check $time -lt $atime
	mtime=`${fstest} stat ${n0} mtime`
	test_check $time -lt $mtime
	ctime=`${fstest} stat ${n0} ctime`
	test_check $time -lt $ctime
	mtime=`${fstest} stat . mtime`
	test_check $time -lt $mtime
	ctime=`${fstest} stat . ctime`
	test_check $time -lt $ctime
	expect 0 unlink ${n0}
done

cd ${cdir}
expect 0 rmdir ${n1}
