#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/open/20.t 211352 2010-08-15 21:24:17Z pjd $

desc="open returns ETXTBSY when the file is a pure procedure (shared text) file that is being executed and the open() system call requests write access"

dir=`dirname $0`
. ${dir}/../misc.sh

[ "${os}:${fs}" = "FreeBSD:UFS" ] || quick_exit
noexec && quick_exit

requires_exec

echo "1..4"

n0=`namegen`

cp -pf `which sleep` ${n0}
./${n0} 3 &
while ! pkill -0 -f ./${n0}; do
	sleep 0.1
done
expect ETXTBSY open ${n0} O_WRONLY
expect ETXTBSY open ${n0} O_RDWR
expect ETXTBSY open ${n0} O_RDONLY,O_TRUNC
pkill -9 -f ./${n0}
expect 0 unlink ${n0}
