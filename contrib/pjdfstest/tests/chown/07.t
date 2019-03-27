#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chown/07.t 211410 2010-08-17 06:08:09Z pjd $

desc="chown returns EPERM if the operation would change the ownership, but the effective user ID is not the super-user and the process is not an owner of the file"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..132"

n0=`namegen`
n1=`namegen`
n2=`namegen`
n3=`namegen`

expect 0 mkdir ${n0} 0755
cdir=`pwd`
cd ${n0}
expect 0 mkdir ${n1} 0755
expect 0 chown ${n1} 65534 65534
for type in regular dir fifo block char socket symlink; do
	if [ "${type}" != "symlink" ]; then
		create_file ${type} ${n1}/${n2} 65534 65534
		expect EPERM -u 65534 -g 65534 chown ${n1}/${n2} 65533 65533
		expect EPERM -u 65533 -g 65533 chown ${n1}/${n2} 65534 65534
		expect EPERM -u 65533 -g 65533 chown ${n1}/${n2} 65533 65533
		expect EPERM -u 65534 -g 65534 -- chown ${n1}/${n2} -1 65533
		expect 0 -u 65534 -g 65534 symlink ${n2} ${n1}/${n3}
		expect EPERM -u 65534 -g 65534 chown ${n1}/${n3} 65533 65533
		expect EPERM -u 65533 -g 65533 chown ${n1}/${n3} 65534 65534
		expect EPERM -u 65533 -g 65533 chown ${n1}/${n3} 65533 65533
		expect EPERM -u 65534 -g 65534 -- chown ${n1}/${n3} -1 65533
		expect 0 unlink ${n1}/${n3}
		if [ "${type}" = "dir" ]; then
			expect 0 rmdir ${n1}/${n2}
		else
			expect 0 unlink ${n1}/${n2}
		fi
	fi
	create_file ${type} ${n1}/${n2} 65534 65534
	expect EPERM -u 65534 -g 65534 lchown ${n1}/${n2} 65533 65533
	expect EPERM -u 65533 -g 65533 lchown ${n1}/${n2} 65534 65534
	expect EPERM -u 65533 -g 65533 lchown ${n1}/${n2} 65533 65533
	expect EPERM -u 65534 -g 65534 -- lchown ${n1}/${n2} -1 65533
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n1}/${n2}
	else
		expect 0 unlink ${n1}/${n2}
	fi
done
expect 0 rmdir ${n1}
cd ${cdir}
expect 0 rmdir ${n0}
