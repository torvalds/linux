#! /bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD$

desc="utimensat with UTIME_NOW will set the will set typestamps to now"

dir=`dirname $0`
. ${dir}/../misc.sh

require "utimensat"

echo "1..7"

n0=`namegen`
n1=`namegen`
TIME_MARGIN=300		# Allow up to a 5 minute delta between the timestamps

expect 0 mkdir ${n1} 0755
cdir=`pwd`
cd ${n1}


create_file regular ${n0}
old_mtime=`$fstest lstat ${n0} mtime`
old_atime=`$fstest lstat ${n0} atime`
sleep 1	# Ensure that future timestamps will be different than this one

expect 0 open . O_RDONLY : utimensat 0 ${n0} 0 UTIME_NOW 0 UTIME_NOW 0
new_mtime=`$fstest lstat ${n0} mtime`
new_atime=`$fstest lstat ${n0} atime`
delta_mtime=$(( $new_mtime - $old_mtime ))
delta_atime=$(( $new_atime - $old_atime ))

if [ "$delta_mtime" -gt 0 ]; then
	if [ "$delta_mtime" -lt $TIME_MARGIN ]; then
		echo "ok 4"
	else
		echo "not ok 4 new mtime is implausibly far in the future"
	fi
else
	echo "not ok 4 mtime was not updated"
fi
if [ "$delta_atime" -gt 0 ]; then
	if [ "$delta_atime" -lt $TIME_MARGIN ]; then
		echo "ok 5"
	else
		echo "not ok 5 new atime is implausibly far in the future"
	fi
else
	echo "not ok 5 atime was not updated"
fi
ntest=$((ntest+2))
expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n1}
