# $NetBSD: t_mkdir.sh,v 1.8 2011/03/05 07:41:11 pooka Exp $
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
# Verifies that mkdir works by creating some nested directories.  It also
# checks, in part, the lookup operation.
#

atf_test_case single
single_head() {
	atf_set "descr" "Creates a single directory and checks the" \
	                "mount point's hard link count"
	atf_set "require.user" "root"
}
single_body() {
	test_mount

	atf_check -s eq:1 -o empty -e empty test -d a
	atf_check -s eq:0 -o empty -e empty mkdir a
	atf_check -s eq:0 -o empty -e empty test -d a
	test -d a
	eval $(stat -s ${Mount_Point})
	[ ${st_nlink} = 3 ] || atf_fail "Link count is not 3"

	test_unmount
}

atf_test_case many
many_head() {
	atf_set "descr" "Creates multiple directories and checks the" \
	                "mount point's hard link count"
	atf_set "require.user" "root"
}
many_body() {
	test_mount

	for d in $(jot 100); do
		atf_check -s eq:1 -o empty -e empty test -d ${d}
		atf_check -s eq:0 -o empty -e empty mkdir ${d}
		atf_check -s eq:0 -o empty -e empty test -d ${d}
	done
	eval $(stat -s ${Mount_Point})
	[ ${st_nlink} = 102 ] || atf_fail "Link count is not 102"

	test_unmount
}

atf_test_case nested
nested_head() {
	atf_set "descr" "Checks if nested directories can be created"
	atf_set "require.user" "root"
}
nested_body() {
	test_mount

	atf_check -s eq:1 -o empty -e empty test -d a/b/c/d/e
	atf_check -s eq:0 -o empty -e empty mkdir -p a/b/c/d/e
	atf_check -s eq:0 -o empty -e empty test -d a/b/c/d/e

	test_unmount
}

atf_test_case attrs
attrs_head() {
	atf_set "descr" "Checks that new directories get the proper" \
	                "attributes (owner and group)"
	atf_set "require.config" "unprivileged-user"
	atf_set "require.user" "root"
}
attrs_body() {
	user=$(atf_config_get unprivileged-user)
	# Allow the unprivileged user to access the work directory.
	chown ${user} .

	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir b c

	atf_check -s eq:0 -o empty -e empty chown ${user}:0 b
	eval $(stat -s b)
	[ ${st_uid} -eq $(id -u ${user}) ] || atf_fail "Incorrect owner"
	[ ${st_gid} -eq 0 ] || atf_fail "Incorrect group"

	atf_check -s eq:0 -o empty -e empty chown ${user}:100 c
	eval $(stat -s c)
	[ ${st_uid} -eq $(id -u ${user}) ] || atf_fail "Incorrect owner"
	[ ${st_gid} -eq 100 ] || atf_fail "Incorrect group"

	atf_check -s eq:0 -o empty -e empty su -m ${user} -c 'mkdir b/a'
	eval $(stat -s b/a)
	[ ${st_uid} -eq $(id -u ${user}) ] || atf_fail "Incorrect owner"
	[ ${st_gid} -eq 0 ] || atf_fail "Incorrect group"

	atf_check -s eq:0 -o empty -e empty su -m ${user} -c 'mkdir c/a'
	eval $(stat -s c/a)
	[ ${st_uid} -eq $(id -u ${user}) ] || atf_fail "Incorrect owner"
	[ ${st_gid} -eq 100 ] || atf_fail "Incorrect group"

	test_unmount
}

atf_test_case kqueue
kqueue_head() {
	atf_set "descr" "Creates a directory and checks the kqueue events" \
	                "raised"
	atf_set "require.user" "root"
}
kqueue_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir dir
	echo 'mkdir dir/a' | kqueue_monitor 2 dir

	# Creating a directory raises NOTE_LINK on the parent directory
	kqueue_check dir NOTE_LINK

	# Creating a directory raises NOTE_WRITE on the parent directory
	kqueue_check dir NOTE_WRITE

	atf_check -s eq:0 -o empty -e empty rmdir dir/a
	atf_check -s eq:0 -o empty -e empty rmdir dir

	test_unmount
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case single
	atf_add_test_case many
	atf_add_test_case nested
	atf_add_test_case attrs
	atf_add_test_case kqueue
}
