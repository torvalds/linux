#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/rename/13.t 211352 2010-08-15 21:24:17Z pjd $

desc="rename returns ENOTDIR when the 'from' argument is a directory, but 'to' is not a directory"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..32"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n0} 0755

for type in regular fifo block char socket symlink; do
	create_file ${type} ${n1}
	expect ENOTDIR rename ${n0} ${n1}
	expect dir lstat ${n0} type
	expect ${type} lstat ${n1} type
	expect 0 unlink ${n1}
done

expect 0 rmdir ${n0}
