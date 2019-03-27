#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chflags/00.t 211352 2010-08-15 21:24:17Z pjd $

desc="chflags changes flags"

dir=`dirname $0`
. ${dir}/../misc.sh

require chflags

case "${os}:${fs}" in
FreeBSD:UFS)
	allflags="UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_NOUNLINK,UF_OPAQUE,SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND,SF_NOUNLINK"
	userflags="UF_NODUMP,UF_IMMUTABLE,UF_APPEND,UF_NOUNLINK,UF_OPAQUE"
	systemflags="SF_ARCHIVED,SF_IMMUTABLE,SF_APPEND,SF_NOUNLINK"

	echo "1..742"
	;;
FreeBSD:ZFS)
	allflags="UF_NODUMP,SF_IMMUTABLE,SF_APPEND,SF_NOUNLINK"
	userflags="UF_NODUMP"
	systemflags="SF_IMMUTABLE,SF_APPEND,SF_NOUNLINK"

	echo "1..482"
	;;
*)
	quick_exit
	;;
esac

n0=`namegen`
n1=`namegen`
n2=`namegen`

expect 0 mkdir ${n2} 0755
cdir=`pwd`
cd ${n2}

for type in regular dir fifo block char socket; do
	create_file ${type} ${n0}
	expect none stat ${n0} flags
	expect 0 chflags ${n0} ${allflags}
	expect ${allflags} stat ${n0} flags
	expect 0 chflags ${n0} ${userflags}
	expect ${userflags} stat ${n0} flags
	expect 0 chflags ${n0} ${systemflags}
	expect ${systemflags} stat ${n0} flags
	expect 0 chflags ${n0} none
	expect none stat ${n0} flags
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n0}
	else
		expect 0 unlink ${n0}
	fi

	create_file ${type} ${n0}
	expect none stat ${n0} flags
	expect 0 lchflags ${n0} ${allflags}
	expect ${allflags} stat ${n0} flags
	expect 0 lchflags ${n0} ${userflags}
	expect ${userflags} stat ${n0} flags
	expect 0 lchflags ${n0} ${systemflags}
	expect ${systemflags} stat ${n0} flags
	expect 0 lchflags ${n0} none
	expect none stat ${n0} flags
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n0}
	else
		expect 0 unlink ${n0}
	fi
done

expect 0 create ${n0} 0644
expect 0 symlink ${n0} ${n1}
expect none stat ${n1} flags
expect none lstat ${n1} flags
expect 0 chflags ${n1} ${allflags}
expect ${allflags} stat ${n1} flags
expect none lstat ${n1} flags
expect 0 chflags ${n1} ${userflags}
expect ${userflags} stat ${n1} flags
expect none lstat ${n1} flags
expect 0 chflags ${n1} ${systemflags}
expect ${systemflags} stat ${n1} flags
expect none lstat ${n1} flags
expect 0 chflags ${n1} none
expect none stat ${n1} flags
expect none lstat ${n1} flags
expect 0 unlink ${n1}
expect 0 unlink ${n0}

expect 0 create ${n0} 0644
expect 0 symlink ${n0} ${n1}
expect none stat ${n1} flags
expect none lstat ${n1} flags
expect 0 lchflags ${n1} ${allflags}
expect ${allflags} lstat ${n1} flags
expect none stat ${n1} flags
expect 0 lchflags ${n1} ${userflags}
expect ${userflags} lstat ${n1} flags
expect none stat ${n1} flags
expect 0 lchflags ${n1} ${systemflags}
expect ${systemflags} lstat ${n1} flags
expect none stat ${n1} flags
expect 0 lchflags ${n1} none
expect none lstat ${n1} flags
expect none stat ${n1} flags
expect 0 unlink ${n1}
expect 0 unlink ${n0}

# successful chflags(2) updates ctime.
for type in regular dir fifo block char socket symlink; do
	if [ "${type}" != "symlink" ]; then
		create_file ${type} ${n0}
		for flag in `echo ${allflags},none | tr ',' ' '`; do
			ctime1=`${fstest} stat ${n0} ctime`
			sleep 1
			expect 0 chflags ${n0} ${flag}
			ctime2=`${fstest} stat ${n0} ctime`
			test_check $ctime1 -lt $ctime2
		done
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n0}
		else
			expect 0 unlink ${n0}
		fi
	fi

	create_file ${type} ${n0}
	for flag in `echo ${allflags},none | tr ',' ' '`; do
		ctime1=`${fstest} lstat ${n0} ctime`
		sleep 1
		expect 0 lchflags ${n0} ${flag}
		ctime2=`${fstest} lstat ${n0} ctime`
		test_check $ctime1 -lt $ctime2
	done
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n0}
	else
		expect 0 unlink ${n0}
	fi
done

# unsuccessful chflags(2) does not update ctime.
for type in regular dir fifo block char socket symlink; do
	if [ "${type}" != "symlink" ]; then
		create_file ${type} ${n0}
		for flag in `echo ${allflags},none | tr ',' ' '`; do
			ctime1=`${fstest} stat ${n0} ctime`
			sleep 1
			expect EPERM -u 65534 chflags ${n0} ${flag}
			ctime2=`${fstest} stat ${n0} ctime`
			test_check $ctime1 -eq $ctime2
		done
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n0}
		else
			expect 0 unlink ${n0}
		fi
	fi

	create_file ${type} ${n0}
	for flag in `echo ${allflags},none | tr ',' ' '`; do
		ctime1=`${fstest} lstat ${n0} ctime`
		sleep 1
		expect EPERM -u 65534 lchflags ${n0} ${flag}
		ctime2=`${fstest} lstat ${n0} ctime`
		test_check $ctime1 -eq $ctime2
	done
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n0}
	else
		expect 0 unlink ${n0}
	fi
done

cd ${cdir}
expect 0 rmdir ${n2}
