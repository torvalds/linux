#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/chflags/01.t 211474 2010-08-18 22:06:43Z pjd $

desc="chflags returns ENOTDIR if a component of the path prefix is not a directory"

dir=`dirname $0`
. ${dir}/../misc.sh

require chflags
if requires_root
then

echo "1..17"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755
for type in regular fifo block char socket; do
	create_file ${type} ${n0}/${n1}
	expect ENOTDIR chflags ${n0}/${n1}/test SF_IMMUTABLE
	expect 0 unlink ${n0}/${n1}
done
expect 0 rmdir ${n0}

else
echo "1..1"
fi
