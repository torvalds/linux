#!/bin/sh
# vim: filetype=sh noexpandtab ts=8 sw=8
# $FreeBSD: head/tools/regression/pjdfstest/tests/open/00.t 211352 2010-08-15 21:24:17Z pjd $

desc="open opens (and eventually creates) a file"

dir=`dirname $0`
. ${dir}/../misc.sh

echo "1..47"

n0=`namegen`
n1=`namegen`

expect 0 mkdir ${n1} 0755
cdir=`pwd`
cd ${n1}

# POSIX: (If O_CREAT is specified and the file doesn't exist) [...] the access
# permission bits of the file mode shall be set to the value of the third
# argument taken as type mode_t modified as follows: a bitwise AND is performed
# on the file-mode bits and the corresponding bits in the complement of the
# process' file mode creation mask. Thus, all bits in the file mode whose
# corresponding bit in the file mode creation mask is set are cleared.
expect 0 open ${n0} O_CREAT,O_WRONLY 0755
expect regular,0755 lstat ${n0} type,mode
expect 0 unlink ${n0}
expect 0 open ${n0} O_CREAT,O_WRONLY 0151
expect regular,0151 lstat ${n0} type,mode
expect 0 unlink ${n0}
expect 0 -U 077 open ${n0} O_CREAT,O_WRONLY 0151
expect regular,0100 lstat ${n0} type,mode
expect 0 unlink ${n0}
expect 0 -U 070 open ${n0} O_CREAT,O_WRONLY 0345
expect regular,0305 lstat ${n0} type,mode
expect 0 unlink ${n0}
expect 0 -U 0501 open ${n0} O_CREAT,O_WRONLY 0345
expect regular,0244 lstat ${n0} type,mode
expect 0 unlink ${n0}

# POSIX: (If O_CREAT is specified and the file doesn't exist) [...] the user ID
# of the file shall be set to the effective user ID of the process; the group ID
# of the file shall be set to the group ID of the file's parent directory or to
# the effective group ID of the process [...]
expect 0 chown . 65535 65535
expect 0 -u 65535 -g 65535 open ${n0} O_CREAT,O_WRONLY 0644
expect 65535,65535 lstat ${n0} uid,gid
expect 0 unlink ${n0}
expect 0 -u 65535 -g 65534 open ${n0} O_CREAT,O_WRONLY 0644
expect "65535,6553[45]" lstat ${n0} uid,gid
expect 0 unlink ${n0}
expect 0 chmod . 0777
expect 0 -u 65534 -g 65533 open ${n0} O_CREAT,O_WRONLY 0644
expect "65534,6553[35]" lstat ${n0} uid,gid
expect 0 unlink ${n0}

# Update parent directory ctime/mtime if file didn't exist.
expect 0 chown . 0 0
time=`${fstest} stat . ctime`
sleep 1
expect 0 open ${n0} O_CREAT,O_WRONLY 0644
atime=`${fstest} stat ${n0} atime`
test_check $time -lt $atime
mtime=`${fstest} stat ${n0} mtime`
test_check $time -lt $mtime
ctime=`${fstest} stat ${n0} ctime`
test_check $time -lt $ctime
mtime=`${fstest} stat . mtime`
test_check $time -lt $mtime
ctime=`${fstest} stat . ctime`
test_check $time -lt $ctime
expect 0 unlink ${n0}

# Don't update parent directory ctime/mtime if file existed.
expect 0 create ${n0} 0644
dmtime=`${fstest} stat . mtime`
dctime=`${fstest} stat . ctime`
sleep 1
expect 0 open ${n0} O_CREAT,O_RDONLY 0644
mtime=`${fstest} stat . mtime`
test_check $dmtime -eq $mtime
ctime=`${fstest} stat . ctime`
test_check $dctime -eq $ctime
expect 0 unlink ${n0}

echo test > ${n0}
expect 5 stat ${n0} size
mtime1=`${fstest} stat ${n0} mtime`
ctime1=`${fstest} stat ${n0} ctime`
sleep 1
expect 0 open ${n0} O_WRONLY,O_TRUNC
mtime2=`${fstest} stat ${n0} mtime`
test_check $mtime1 -lt $mtime2
ctime2=`${fstest} stat ${n0} ctime`
test_check $ctime1 -lt $ctime2
expect 0 stat ${n0} size
expect 0 unlink ${n0}

cd ${cdir}
expect 0 rmdir ${n1}
