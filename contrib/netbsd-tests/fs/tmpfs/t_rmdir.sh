# $NetBSD: t_rmdir.sh,v 1.5 2010/11/07 17:51:18 jmmv Exp $
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
# Verifies that rmdir works by creating and removing directories.  Also
# checks multiple error conditions.
#

atf_test_case mntpt
mntpt_head() {
	atf_set "descr" "Checks that the mount point cannot be removed"
	atf_set "require.user" "root"
}
mntpt_body() {
	test_mount

	atf_check -s eq:1 -o empty -e ignore rmdir ${Mount_Point}

	test_unmount
}

atf_test_case non_existent
non_existent_head() {
	atf_set "descr" "Checks that non-existent directories cannot" \
	                "be removed"
	atf_set "require.user" "root"
}
non_existent_body() {
	test_mount

	atf_check -s eq:1 -o empty -e ignore rmdir non-existent

	test_unmount
}

atf_test_case single
single_head() {
	atf_set "descr" "Checks that removing a single directory works"
	atf_set "require.user" "root"
}
single_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir a
	eval $(stat -s ${Mount_Point})
	[ ${st_nlink} = 3 ] || \
	    atf_fail "Incorrect number of links after creation"
	atf_check -s eq:0 -o empty -e empty rmdir a
	eval $(stat -s ${Mount_Point})
	[ ${st_nlink} = 2 ] || \
	    atf_fail "Incorrect number of links after removal"

	test_unmount
}

atf_test_case nested
nested_head() {
	atf_set "descr" "Checks that removing nested directories works"
	atf_set "require.user" "root"
}
nested_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir -p a/b/c
	atf_check -s eq:0 -o empty -e empty rmdir a/b/c
	atf_check -s eq:0 -o empty -e empty rmdir a/b
	atf_check -s eq:0 -o empty -e empty rmdir a

	test_unmount
}

atf_test_case dots
dots_head() {
	atf_set "descr" "Checks that '.' and '..' cannot be removed"
	atf_set "require.user" "root"
}
dots_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir a
	atf_check -s eq:1 -o empty -e ignore rmdir a/.
	atf_check -s eq:1 -o empty -e ignore rmdir a/..
	atf_check -s eq:0 -o empty -e empty rmdir a

	test_unmount
}

atf_test_case non_empty
non_empty_head() {
	atf_set "descr" "Checks that non-empty directories cannot be removed"
	atf_set "require.user" "root"
}
non_empty_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir a
	atf_check -s eq:0 -o empty -e empty mkdir a/b
	atf_check -s eq:0 -o empty -e empty mkdir a/c
	atf_check -s eq:1 -o empty -e ignore rmdir a
	atf_check -s eq:0 -o empty -e empty rmdir a/b
	atf_check -s eq:0 -o empty -e empty rmdir a/c
	atf_check -s eq:0 -o empty -e empty rmdir a

	test_unmount
}

atf_test_case links
links_head() {
	atf_set "descr" "Checks the root directory's links after removals"
	atf_set "require.user" "root"
}
links_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir a
	atf_check -s eq:0 -o empty -e empty mkdir a/b
	atf_check -s eq:0 -o empty -e empty mkdir c

	atf_check -s eq:0 -o empty -e empty rmdir c
	atf_check -s eq:0 -o empty -e empty rmdir a/b
	atf_check -s eq:0 -o empty -e empty rmdir a

	eval $(stat -s ${Mount_Point})
	[ ${st_nlink} = 2 ] || atf_fail "Incorrect number of links"

	test_unmount
}

atf_test_case curdir
curdir_head() {
	atf_set "descr" "Checks that the current directory cannot be removed"
	atf_set "require.user" "root"
}
curdir_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir a
	# Catch a bug that would panic the system when accessing the
	# current directory after being deleted: vop_open cannot assume
	# that open files are still linked to a directory.
	atf_check -s eq:1 -o empty -e ignore -x '( cd a && rmdir ../a && ls )'
	atf_check -s eq:1 -o empty -e empty test -e a

	test_unmount
}

atf_test_case kqueue
kqueue_head() {
	atf_set "descr" "Checks that removing a directory raises the" \
	                "correct kqueue events"
	atf_set "require.user" "root"
}
kqueue_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir dir
	atf_check -s eq:0 -o empty -e empty mkdir dir/a
	echo 'rmdir dir/a' | kqueue_monitor 3 dir dir/a
	kqueue_check dir/a NOTE_DELETE
	kqueue_check dir NOTE_LINK
	kqueue_check dir NOTE_WRITE
	atf_check -s eq:0 -o empty -e empty rmdir dir

	test_unmount
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case mntpt
	atf_add_test_case non_existent
	atf_add_test_case single
	atf_add_test_case nested
	atf_add_test_case dots
	atf_add_test_case non_empty
	atf_add_test_case links
	atf_add_test_case curdir
	atf_add_test_case kqueue
}
