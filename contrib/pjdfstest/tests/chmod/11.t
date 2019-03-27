#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chmod/11.t 211352 2010-08-15 21:24:17Z pjd $

desc="chmod returns EFTYPE if the effective user ID is not the super-user, the mode includes the sticky bit (S_ISVTX), and path does not refer to a directory"

dir=`dirname $0`
. ${dir}/../misc.sh

if supported lchmod; then
	echo "1..173"
else
	echo "1..109"
fi

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n0} 0755
cdir=`pwd`
cd ${n0}

for type in regular dir fifo block char socket symlink; do
	if [ "${type}" != "symlink" ]; then
		create_file ${type} ${n1}
		expect 0 chmod ${n1} 01621
		expect 01621 stat ${n1} mode
		expect 0 symlink ${n1} ${n2}
		expect 0 chmod ${n2} 01700
		expect 01700 stat ${n1} mode
		expect 0 unlink ${n2}
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n1}
		else
			expect 0 unlink ${n1}
		fi
	fi

	if supported lchmod; then
		create_file ${type} ${n1}
		expect 0 lchmod ${n1} 01621
		expect 01621 lstat ${n1} mode
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n1}
		else
			expect 0 unlink ${n1}
		fi
	fi
done

expect 0 mkdir ${n1} 0755
expect 0 chown ${n1} 65534 65534
expect 0 -u 65534 -g 65534 chmod ${n1} 01755
expect 01755 stat ${n1} mode
expect 0 symlink ${n1} ${n2}
expect 0 chmod ${n2} 01700
expect 01700 stat ${n1} mode
expect 0 unlink ${n2}
expect 0 rmdir ${n1}

for type in regular fifo block char socket symlink; do
	if [ "${type}" != "symlink" ]; then
		create_file ${type} ${n1} 0640 65534 65534
		expect 0 symlink ${n1} ${n2}
		case "${os}" in
		Darwin)
			expect 0 -u 65534 -g 65534 chmod ${n1} 01644
			expect 01644 stat ${n1} mode
			expect 0 -u 65534 -g 65534 chmod ${n2} 01640
			expect 01640 stat ${n1} mode
			;;
		FreeBSD)
			expect EFTYPE -u 65534 -g 65534 chmod ${n1} 01644
			expect 0640 stat ${n1} mode
			expect EFTYPE -u 65534 -g 65534 chmod ${n2} 01644
			expect 0640 stat ${n1} mode
			;;
		SunOS)
			expect 0 -u 65534 -g 65534 chmod ${n1} 01644
			expect 0644 stat ${n1} mode
			expect 0 -u 65534 -g 65534 chmod ${n2} 01640
			expect 0640 stat ${n1} mode
			;;
		Linux)
			expect 0 -u 65534 -g 65534 chmod ${n1} 01644
			expect 01644 stat ${n1} mode
			expect 0 -u 65534 -g 65534 chmod ${n2} 01640
			expect 01640 stat ${n1} mode
			;;
		esac
		expect 0 unlink ${n2}
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n1}
		else
			expect 0 unlink ${n1}
		fi
	fi

	if supported lchmod; then
		create_file ${type} ${n1} 0640 65534 65534
		case "${os}" in
		Darwin)
			expect 0 -u 65534 -g 65534 lchmod ${n1} 01644
			expect 01644 lstat ${n1} mode
			;;
		FreeBSD)
			expect EFTYPE -u 65534 -g 65534 lchmod ${n1} 01644
			expect 0640 lstat ${n1} mode
			;;
		SunOS)
			expect 0 -u 65534 -g 65534 lchmod ${n1} 01644
			expect 0644 lstat ${n1} mode
			;;
		Linux)
			expect 0 -u 65534 -g 65534 lchmod ${n1} 01644
			expect 01644 lstat ${n1} mode
			;;
		esac
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n1}
		else
			expect 0 unlink ${n1}
		fi
	fi
done

cd ${cdir}
expect 0 rmdir ${n0}
