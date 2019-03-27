#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/open/24.t 211352 2010-08-15 21:24:17Z pjd $

dir=`dirname $0`
. ${dir}/../misc.sh

# POSIX doesn't explicitly state the errno for open(2)'ing sockets.
case ${os} in
Darwin|FreeBSD)
	expected_error=EOPNOTSUPP
	;;
Linux)
	expected_error=ENXIO
	;;
*)
	echo "1..0 # SKIP: unsupported OS: ${os}"
	exit 0
	;;
esac

desc="open returns $expected_error when trying to open UNIX domain socket"

echo "1..5"

n0=`namegen`

expect 0 bind ${n0}
expect $expected_error open ${n0} O_RDONLY
expect $expected_error open ${n0} O_WRONLY
expect $expected_error open ${n0} O_RDWR
expect 0 unlink ${n0}
