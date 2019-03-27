#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/open/21.t 211352 2010-08-15 21:24:17Z pjd $

desc="open returns EFAULT if the path argument points outside the process's allocated address space"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..2"

expect EFAULT open NULL O_RDONLY
expect EFAULT open DEADCODE O_RDONLY
