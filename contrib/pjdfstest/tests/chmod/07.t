#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chmod/07.t 211352 2010-08-15 21:24:17Z pjd $

desc="chmod returns EPERM if the operation would change the ownership, but the effective user ID is not the super-user"

dir=`dirname $0`
. ${dir}/../misc.sh

if supported lchmod; then
	echo "1..34"
else
	echo "1..25"
fi

n0=`namegen`
n1=`namegen`
n2=`namegen`
n3=`namegen`

expect 0 mkdir ${n0} 0755
cdir=`pwd`
cd ${n0}
expect 0 mkdir ${n1} 0755
expect 0 chown ${n1} 65534 65534

expect 0 -u 65534 -g 65534 create ${n1}/${n2} 0644
expect 0 -u 65534 -g 65534 chmod ${n1}/${n2} 0642
expect 0642 stat ${n1}/${n2} mode
expect EPERM -u 65533 -g 65533 chmod ${n1}/${n2} 0641
expect 0642 stat ${n1}/${n2} mode
expect 0 chown ${n1}/${n2} 0 0
expect EPERM -u 65534 -g 65534 chmod ${n1}/${n2} 0641
expect 0642 stat ${n1}/${n2} mode
expect 0 unlink ${n1}/${n2}

expect 0 -u 65534 -g 65534 create ${n1}/${n2} 0644
expect 0 -u 65534 -g 65534 symlink ${n2} ${n1}/${n3}
expect 0 -u 65534 -g 65534 chmod ${n1}/${n3} 0642
expect 0642,65534,65534 stat ${n1}/${n2} mode,uid,gid
expect EPERM -u 65533 -g 65533 chmod ${n1}/${n3} 0641
expect 0642,65534,65534 stat ${n1}/${n2} mode,uid,gid
expect 0 chown ${n1}/${n3} 0 0
expect EPERM -u 65534 -g 65534 chmod ${n1}/${n3} 0641
expect 0642,0,0 stat ${n1}/${n2} mode,uid,gid
expect 0 unlink ${n1}/${n2}
expect 0 unlink ${n1}/${n3}

if supported lchmod; then
	expect 0 -u 65534 -g 65534 create ${n1}/${n2} 0644
	expect 0 -u 65534 -g 65534 lchmod ${n1}/${n2} 0642
	expect 0642 stat ${n1}/${n2} mode
	expect EPERM -u 65533 -g 65533 lchmod ${n1}/${n2} 0641
	expect 0642 stat ${n1}/${n2} mode
	expect 0 chown ${n1}/${n2} 0 0
	expect EPERM -u 65534 -g 65534 lchmod ${n1}/${n2} 0641
	expect 0642 stat ${n1}/${n2} mode
	expect 0 unlink ${n1}/${n2}
fi

expect 0 rmdir ${n1}
cd ${cdir}
expect 0 rmdir ${n0}
