# $NetBSD: t_mknod.sh,v 1.5 2010/11/07 17:51:18 jmmv Exp $
#
# Copyright (c) 2005, 2006, 2007, 2008 The NetBSD Foundation, Inc.
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

#
# Verifies that the mknod operation works.
#

atf_test_case block
block_head() {
	atf_set "descr" "Tests that block devices can be created"
	atf_set "require.user" "root"
}
block_body() {
	test_mount
	umask 022

	atf_check -s eq:0 -o empty -e empty mknod fd0a b 2 0
	eval $(stat -s fd0a)
	[ ${st_mode} = 060644 ] || atf_fail "Invalid mode"
	[ ${st_rdev} -eq 512 ] || atf_fail "Invalid device"

	test_unmount
}

atf_test_case block_kqueue
block_kqueue_head() {
	atf_set "descr" "Tests that creating a block device raises the" \
	                "appropriate kqueue events"
	atf_set "require.user" "root"
}
block_kqueue_body() {
	test_mount
	umask 022

	atf_check -s eq:0 -o empty -e empty mkdir dir
	echo 'mknod dir/fd0a b 2 0' | kqueue_monitor 1 dir
	kqueue_check dir NOTE_WRITE

	test_unmount
}

atf_test_case char
char_head() {
	atf_set "descr" "Tests that character devices can be created"
	atf_set "require.user" "root"
}
char_body() {
	test_mount
	umask 022

	atf_check -s eq:0 -o empty -e empty mknod null c 2 2
	eval $(stat -s null)
	[ ${st_mode} = 020644 ] || atf_fail "Invalid mode"
	[ ${st_rdev} -eq 514 ] || atf_fail "Invalid device"

	test_unmount
}

atf_test_case char_kqueue
char_kqueue_head() {
	atf_set "descr" "Tests that creating a character device raises the" \
	                "appropriate kqueue events"
	atf_set "require.user" "root"
}
char_kqueue_body() {
	test_mount
	umask 022

	atf_check -s eq:0 -o empty -e empty mkdir dir
	echo 'mknod dir/null c 2 2' | kqueue_monitor 1 dir
	kqueue_check dir NOTE_WRITE

	test_unmount
}

atf_test_case pipe
pipe_head() {
	atf_set "descr" "Tests that named pipes can be created"
	atf_set "require.user" "root"
}
pipe_body() {
	test_mount
	umask 022

	atf_check -s eq:0 -o empty -e empty mknod pipe p
	eval $(stat -s pipe)
	[ ${st_mode} = 010644 ] || atf_fail "Invalid mode"

	test_unmount
}

atf_test_case pipe_kqueue
pipe_kqueue_head() {
	atf_set "descr" "Tests that creating a named pipe raises the" \
	                "appropriate kqueue events"
	atf_set "require.user" "root"
}
pipe_kqueue_body() {
	test_mount
	umask 022

	atf_check -s eq:0 -o empty -e empty mkdir dir
	echo 'mknod dir/pipe p' | kqueue_monitor 1 dir
	kqueue_check dir NOTE_WRITE

	test_unmount
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case block
	atf_add_test_case block_kqueue
	atf_add_test_case char
	atf_add_test_case char_kqueue
	atf_add_test_case pipe
	atf_add_test_case pipe_kqueue
}
