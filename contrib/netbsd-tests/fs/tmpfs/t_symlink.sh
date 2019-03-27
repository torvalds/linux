# $NetBSD: t_symlink.sh,v 1.5 2010/11/07 17:51:18 jmmv Exp $
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
# Verifies that the symlink and readlink operations work.
#

atf_test_case file
file_head() {
	atf_set "descr" "Tests that symlinks to files work"
	atf_set "require.user" "root"
}
file_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty touch a
	atf_check -s eq:0 -o empty -e empty ln -s a b
	[ $(md5 b | cut -d ' ' -f 4) = d41d8cd98f00b204e9800998ecf8427e ] || \
	    atf_fail "Symlink points to an incorrect file"

	atf_check -s eq:0 -o empty -e empty -x 'echo foo >a'
	[ $(md5 b | cut -d ' ' -f 4) = d3b07384d113edec49eaa6238ad5ff00 ] || \
	    atf_fail "Symlink points to an incorrect file"

	test_unmount
}

atf_test_case exec
exec_head() {
	atf_set "descr" "Tests symlinking to a known system binary and" \
	                "executing it through the symlink"
	atf_set "require.user" "root"
}
exec_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty touch b
	atf_check -s eq:0 -o empty -e empty ln -s /bin/cp cp
	atf_check -s eq:0 -o empty -e empty ./cp b c
	atf_check -s eq:0 -o empty -e empty test -f c

	test_unmount
}

atf_test_case dir
dir_head() {
	atf_set "descr" "Tests that symlinks to directories work"
	atf_set "require.user" "root"
}
dir_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir d
	atf_check -s eq:1 -o empty -e empty test -f d/foo
	atf_check -s eq:1 -o empty -e empty test -f e/foo
	atf_check -s eq:0 -o empty -e empty ln -s d e
	atf_check -s eq:0 -o empty -e empty touch d/foo
	atf_check -s eq:0 -o empty -e empty test -f d/foo
	atf_check -s eq:0 -o empty -e empty test -f e/foo

	test_unmount
}

atf_test_case kqueue
kqueue_head() {
	atf_set "descr" "Tests that creating a symlink raises the" \
	                "appropriate kqueue events"
	atf_set "require.user" "root"
}
kqueue_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir dir
	echo 'ln -s non-existent dir/a' | kqueue_monitor 1 dir
	kqueue_check dir NOTE_WRITE
	atf_check -s eq:0 -o empty -e empty rm dir/a
	atf_check -s eq:0 -o empty -e empty rmdir dir

	test_unmount
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case file
	atf_add_test_case exec
	atf_add_test_case dir
	atf_add_test_case kqueue
}
