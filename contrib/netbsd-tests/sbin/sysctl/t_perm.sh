# $NetBSD: t_perm.sh,v 1.7 2016/06/17 03:55:35 pgoyette Exp $
#
# Copyright (c) 2011 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Jukka Ruohonen.
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
file="/tmp/d_sysctl.out"

clean() {

	if [ -f $file ]; then
		rm $file
	fi
}

sysctl_write() {

	deadbeef="3735928559"
	deadbeef_signed="-559038737"

	sysctl $1 | cut -d= -f1 > $file

	if [ ! -f $file ]; then
		atf_fail "sysctl failed"
	fi

	while read line; do

		node=$(echo $line)

		case $node in

		"$1."*)
			atf_check -s not-exit:0 -e ignore \
				-x sysctl -w $node=$deadbeef
			;;
		esac

	done < $file

	# A functional verification that $deadbeef
	# was not actually written to the node.
	#
	if [ ! -z $(sysctl $1 | grep -e $deadbeef -e $deadbeef_signed) ]; then
		atf_fail "value was written"
	fi
}

# ddb.
#
atf_test_case sysctl_ddb cleanup
sysctl_ddb_head() {
	atf_set "require.user" "unprivileged"
	atf_set "descr" "Test writing to 'ddb' sysctl node as an user"
}

sysctl_ddb_body() {
	sysctl_write "ddb"
}

sysctl_ddb_cleanup() {
	clean
}

# hw.
#
atf_test_case sysctl_hw cleanup
sysctl_hw_head() {
	atf_set "require.user" "unprivileged"
	atf_set "descr" "Test writing to 'hw' sysctl node as an user"
}

sysctl_hw_body() {
	sysctl_write "hw"
}

sysctl_hw_cleanup() {
	clean
}

# kern.
#
atf_test_case sysctl_kern cleanup
sysctl_kern_head() {
	atf_set "require.user" "unprivileged"
	atf_set "descr" "Test writing to 'kern' " \
		"sysctl node as an user (PR kern/44946)"
}

sysctl_kern_body() {
	sysctl_write "kern"
}

sysctl_kern_cleanup() {
	clean
}

# machdep.
#
atf_test_case sysctl_machdep cleanup
sysctl_machdep_head() {
	atf_set "require.user" "unprivileged"
	atf_set "descr" "Test writing to 'machdep' sysctl node as an user"
}

sysctl_machdep_body() {
	sysctl_write "machdep"
}

sysctl_machdep_cleanup() {
	clean
}

# net.
#
atf_test_case sysctl_net cleanup
sysctl_net_head() {
	atf_set "require.user" "unprivileged"
	atf_set "descr" "Test writing to 'net' sysctl node as an user"
}

sysctl_net_body() {
	sysctl_write "net"
}

sysctl_net_cleanup() {
	clean
}

# security.
#
atf_test_case sysctl_security cleanup
sysctl_security_head() {
	atf_set "require.user" "unprivileged"
	atf_set "descr" "Test writing to 'security' sysctl node as an user"
}

sysctl_security_body() {
	sysctl_write "security"
}

sysctl_security_cleanup() {
	clean
}

# vfs.
#
atf_test_case sysctl_vfs cleanup
sysctl_vfs_head() {
	atf_set "require.user" "unprivileged"
	atf_set "descr" "Test writing to 'vfs' sysctl node as an user"
}

sysctl_vfs_body() {
	sysctl_write "vfs"
}

sysctl_vfs_cleanup() {
	clean
}

# vm.
#
atf_test_case sysctl_vm cleanup
sysctl_vm_head() {
	atf_set "require.user" "unprivileged"
	atf_set "descr" "Test writing to 'vm' sysctl node as an user"
}

sysctl_vm_body() {
	sysctl_write "vm"
}

sysctl_vm_cleanup() {
	clean
}

atf_init_test_cases() {
	atf_add_test_case sysctl_ddb
	atf_add_test_case sysctl_hw
	atf_add_test_case sysctl_kern
	atf_add_test_case sysctl_machdep
	atf_add_test_case sysctl_net
	atf_add_test_case sysctl_security
	atf_add_test_case sysctl_vfs
	atf_add_test_case sysctl_vm
}
