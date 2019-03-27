# $NetBSD: t_psshfs.sh,v 1.8 2016/09/05 08:53:57 christos Exp $
#
# Copyright (c) 2007, 2008 The NetBSD Foundation, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
# ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

# -------------------------------------------------------------------------
# Auxiliary functions.
# -------------------------------------------------------------------------

#
# Skips the calling test case if puffs is not supported in the kernel
# or if the calling user does not have the necessary permissions to mount
# file systems.
#
require_puffs() {
	case "$($(atf_get_srcdir)/h_have_puffs)" in
		eacces)
			atf_skip "Cannot open /dev/puffs for read/write access"
			;;
		enxio)
			atf_skip "puffs support not built into the kernel"
			;;
		failed)
			atf_skip "Unknown error trying to access /dev/puffs"
			;;
		yes)
			;;
		*)
			atf_fail "Unknown value returned by h_have_puffs"
			;;
	esac

	if [ $(id -u) -ne 0 -a $(sysctl -n vfs.generic.usermount) -eq 0 ]
	then
		atf_skip "Regular users cannot mount file systems" \
		    "(vfs.generic.usermount is set to 0)"
	fi
}

#
# Starts a SSH server and sets up the client to access it.
# Authentication is allowed and done using an RSA key exclusively, which
# is generated on the fly as part of the test case.
# XXX: Ideally, all the tests in this test program should be able to share
# the generated key, because creating it can be a very slow process on some
# machines.
#
start_ssh() {
	echo "Setting up SSH server configuration"
	sed -e "s,@SRCDIR@,$(atf_get_srcdir),g" -e "s,@WORKDIR@,$(pwd),g" \
	    $(atf_get_srcdir)/sshd_config.in >sshd_config || \
	    atf_fail "Failed to create sshd_config"
	atf_check -s eq:0 -o empty -e empty cp /usr/libexec/sftp-server .
	atf_check -s eq:0 -o empty -e empty \
	    cp $(atf_get_srcdir)/ssh_host_key .
	atf_check -s eq:0 -o empty -e empty \
	    cp $(atf_get_srcdir)/ssh_host_key.pub .
	atf_check -s eq:0 -o empty -e empty chmod 400 ssh_host_key
	atf_check -s eq:0 -o empty -e empty chmod 444 ssh_host_key.pub

	/usr/sbin/sshd -e -f ./sshd_config >sshd.log 2>&1 &
	while [ ! -f sshd.pid ]; do
		sleep 0.01
	done
	echo "SSH server started (pid $(cat sshd.pid))"

	echo "Setting up SSH client configuration"
	atf_check -s eq:0 -o empty -e empty \
	    ssh-keygen -f ssh_user_key -t rsa -b 1024 -N "" -q
	atf_check -s eq:0 -o empty -e empty \
	    cp ssh_user_key.pub authorized_keys
	echo "[localhost]:10000,[127.0.0.1]:10000,[::1]:10000" \
	    "$(cat $(atf_get_srcdir)/ssh_host_key.pub)" >known_hosts || \
	    atf_fail "Failed to create known_hosts"
	atf_check -s eq:0 -o empty -e empty chmod 600 authorized_keys
	sed -e "s,@SRCDIR@,$(atf_get_srcdir),g" -e "s,@WORKDIR@,$(pwd),g" \
	    $(atf_get_srcdir)/ssh_config.in >ssh_config || \
	    atf_fail "Failed to create ssh_config"
}

#
# Stops the SSH server spawned by start_ssh and prints diagnosis data.
#
stop_ssh() {
	if [ -f sshd.pid ]; then
		echo "Stopping SSH server (pid $(cat sshd.pid))"
		kill $(cat sshd.pid)
	fi
	if [ -f sshd.log ]; then
		echo "Server output was:"
		sed -e 's,^,    ,' sshd.log
	fi
}

#
# Mounts the given source directory on the target directory using psshfs.
# Both directories are supposed to live on the current directory.
#
mount_psshfs() {
	atf_check -s eq:0 -o empty -e empty \
	    mount -t psshfs -o -F=$(pwd)/ssh_config localhost:$(pwd)/${1} ${2}
}

# -------------------------------------------------------------------------
# The test cases.
# -------------------------------------------------------------------------

atf_test_case inode_nos cleanup
inode_nos_head() {
	atf_set "descr" "Checks that different files get different inode" \
	    "numbers"
}
inode_nos_body() {
	require_puffs

	start_ssh

	mkdir root
	mkdir root/dir
	touch root/dir/file1
	touch root/dir/file2
	touch root/file3
	touch root/file4

	cat >ne_inodes.sh <<EOF
#! /bin/sh
#
# Compares the inodes of the two given files and returns true if they are
# different; false otherwise.
#
set -e
ino1=\$(stat -f %i \${1})
ino2=\$(stat -f %i \${2})
test \${ino1} -ne \${ino2}
EOF
	chmod +x ne_inodes.sh

	mkdir mnt
	mount_psshfs root mnt
	atf_check -s eq:0 -o empty -e empty \
	    ./ne_inodes.sh root/dir root/dir/file1
	atf_check -s eq:0 -o empty -e empty \
	    ./ne_inodes.sh root/dir root/dir/file2
	atf_check -s eq:0 -o empty -e empty \
	    ./ne_inodes.sh root/dir/file1 root/dir/file2
	atf_check -s eq:0 -o empty -e empty \
	    ./ne_inodes.sh root/file3 root/file4
}
inode_nos_cleanup() {
	umount mnt
	stop_ssh
}

atf_test_case pwd cleanup
pwd_head() {
	atf_set "descr" "Checks that pwd works correctly"
}
pwd_body() {
	require_puffs

	start_ssh

	mkdir root
	mkdir root/dir

	mkdir mnt
	atf_check -s eq:0 -o save:stdout -e empty \
	    -x 'echo $(cd mnt && /bin/pwd)/dir'
	mv stdout expout
	mount_psshfs root mnt
	atf_check -s eq:0 -o file:expout -e empty \
	    -x 'cd mnt/dir && ls .. >/dev/null && /bin/pwd'
}
pwd_cleanup() {
	umount mnt
	stop_ssh
}

atf_test_case ls cleanup
ls_head() {
	atf_set "descr" "Uses ls, attempts to exercise puffs_cc"
}
ls_body() {
	require_puffs

	start_ssh

	mkdir mnt
	mkdir root
	mkdir root/dir
	touch root/dir/file1
	touch root/dir/file2
	touch root/file3
	touch root/file4

	mount_psshfs root mnt

	ls -l mnt &

	IFS=' '
lsout='dir
file3
file4

mnt/dir:
file1
file2
'
	atf_check -s exit:0 -o inline:"$lsout" ls -R mnt
}
ls_cleanup() {
	umount mnt
	stop_ssh
}

atf_test_case setattr_cache cleanup
setattr_cache_head() {
	atf_set "descr" "Checks that setattr caches"
	# Don't wait for the eternity that atf usually waits.  Twenty
	# seconds should be good enough, except maybe on a VAX...
	atf_set "timeout" 20
}
setattr_cache_body() {
	require_puffs
	start_ssh
	atf_check -s exit:0 mkdir root
	atf_check -s exit:0 mkdir mnt
	mount_psshfs root mnt
	atf_check -s exit:0 -x ': > mnt/loser'
	atf_check -s exit:0 -o save:stat stat mnt/loser
	# Oops -- this doesn't work.  We need to stop the child of the
	# sshd that is handling the sftp session.
	atf_check -s exit:0 kill -STOP $(cat sshd.pid)
	atf_check -s exit:0 -x ': > mnt/loser'
	atf_check -s exit:0 -o file:stat stat mnt/loser
}
setattr_cache_cleanup() {
	umount mnt
	kill -CONT $(cat sshd.pid)
	stop_ssh
}

atf_test_case read_empty_file cleanup
read_empty_file_head() {
	atf_set "descr" "Checks whether an empty file can be read"
	# This test is supposed to make sure psshfs does not hang
	# when reading from an empty file, hence the timeout.
	atf_set "timeout" 8
}
read_empty_file_body() {
	require_puffs
	start_ssh
	atf_check mkdir root mnt
	atf_check -x ': > root/empty'
	mount_psshfs root mnt
	atf_check cat mnt/empty
}
read_empty_file_cleanup() {
	umount mnt
	stop_ssh
}

# -------------------------------------------------------------------------
# Initialization.
# -------------------------------------------------------------------------

atf_init_test_cases() {
	atf_add_test_case inode_nos
	atf_add_test_case pwd
	atf_add_test_case ls
	#atf_add_test_case setattr_cache
	atf_add_test_case read_empty_file
}
