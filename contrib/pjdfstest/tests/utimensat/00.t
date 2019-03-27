#! /bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD$

desc="utimensat changes timestamps on any type of file"

dir=`dirname $0`
. ${dir}/../misc.sh

require "utimensat"

echo "1..32"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n1} 0755
cdir=`pwd`
cd ${n1}

DATE1=1900000000 #Sun Mar 17 11:46:40 MDT 2030
DATE2=1950000000 #Fri Oct 17 04:40:00 MDT 2031
for type in regular dir fifo block char socket; do
	create_file ${type} ${n0}
	expect 0 open . O_RDONLY : utimensat 0 ${n0} $DATE1 0 $DATE2 0 0
	expect $DATE1 lstat ${n0} atime
	expect $DATE2 lstat ${n0} mtime
	if [ "${type}" = "dir" ]; then
		expect 0 rmdir ${n0}
	else
		expect 0 unlink ${n0}
	fi
done

cd ${cdir}
expect 0 rmdir ${n1}
