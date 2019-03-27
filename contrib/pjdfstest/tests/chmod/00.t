#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chmod/00.t 211352 2010-08-15 21:24:17Z pjd $

desc="chmod changes permission"

dir=`dirname $0`
. ${dir}/../misc.sh

if supported lchmod; then
	echo "1..203"
else
	echo "1..119"
fi

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n2} 0755
cdir=`pwd`
cd ${n2}

for type in regular dir fifo block char socket symlink; do
	if [ "${type}" != "symlink" ]; then
		create_file ${type} ${n0}
		expect 0 chmod ${n0} 0111
		expect 0111 stat ${n0} mode

		expect 0 symlink ${n0} ${n1}
		mode=`${fstest} lstat ${n1} mode`
		expect 0 chmod ${n1} 0222
		expect 0222 stat ${n1} mode
		expect 0222 stat ${n0} mode
		expect ${mode} lstat ${n1} mode
		expect 0 unlink ${n1}

		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n0}
		else
			expect 0 unlink ${n0}
		fi
	fi

	if supported lchmod; then
		create_file ${type} ${n0}
		expect 0 lchmod ${n0} 0111
		expect 0111 lstat ${n0} mode
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n0}
		else
			expect 0 unlink ${n0}
		fi
	fi
done

# successful chmod(2) updates ctime.
for type in regular dir fifo block char socket symlink; do
	if [ "${type}" != "symlink" ]; then
		create_file ${type} ${n0}
		ctime1=`${fstest} stat ${n0} ctime`
		sleep 1
		expect 0 chmod ${n0} 0111
		ctime2=`${fstest} stat ${n0} ctime`
		test_check $ctime1 -lt $ctime2
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n0}
		else
			expect 0 unlink ${n0}
		fi
	fi

	if supported lchmod; then
		create_file ${type} ${n0}
		ctime1=`${fstest} lstat ${n0} ctime`
		sleep 1
		expect 0 lchmod ${n0} 0111
		ctime2=`${fstest} lstat ${n0} ctime`
		test_check $ctime1 -lt $ctime2
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n0}
		else
			expect 0 unlink ${n0}
		fi
	fi
done

# unsuccessful chmod(2) does not update ctime.
for type in regular dir fifo block char socket symlink; do
	if [ "${type}" != "symlink" ]; then
		create_file ${type} ${n0}
		ctime1=`${fstest} stat ${n0} ctime`
		sleep 1
		expect EPERM -u 65534 chmod ${n0} 0111
		ctime2=`${fstest} stat ${n0} ctime`
		test_check $ctime1 -eq $ctime2
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n0}
		else
			expect 0 unlink ${n0}
		fi
	fi

	if supported lchmod; then
		create_file ${type} ${n0}
		ctime1=`${fstest} lstat ${n0} ctime`
		sleep 1
		expect EPERM -u 65534 lchmod ${n0} 0321
		ctime2=`${fstest} lstat ${n0} ctime`
		test_check $ctime1 -eq $ctime2
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n0}
		else
			expect 0 unlink ${n0}
		fi
	fi
done

# POSIX: If the calling process does not have appropriate privileges, and if
# the group ID of the file does not match the effective group ID or one of the
# supplementary group IDs and if the file is a regular file, bit S_ISGID
# (set-group-ID on execution) in the file's mode shall be cleared upon
# successful return from chmod().

expect 0 create ${n0} 0755
expect 0 chown ${n0} 65535 65535
expect 0 -u 65535 -g 65535 chmod ${n0} 02755
expect 02755 stat ${n0} mode
expect 0 -u 65535 -g 65535 chmod ${n0} 0755
expect 0755 stat ${n0} mode

todo FreeBSD "S_ISGID should be removed and chmod(2) should success and FreeBSD returns EPERM."
expect 0 -u 65535 -g 65534 chmod ${n0} 02755
expect 0755 stat ${n0} mode

expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n2}
