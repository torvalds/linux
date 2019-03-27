# $NetBSD: t_rename.sh,v 1.5 2010/11/07 17:51:18 jmmv Exp $
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
# Verifies that the rename operation works (either by renaming entries or
# by moving them).
#

atf_test_case dots
dots_head() {
	atf_set "descr" "Tests that '.' and '..' cannot be renamed"
	atf_set "require.user" "root"
}
dots_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir a
	atf_check -s eq:1 -o empty -e ignore mv a/. c
	atf_check -s eq:1 -o empty -e ignore mv a/.. c
	atf_check -s eq:0 -o empty -e empty rmdir a

	test_unmount
}

atf_test_case crossdev
crossdev_head() {
	atf_set "descr" "Tests that cross-device renames do not work"
	atf_set "require.user" "root"
}
crossdev_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir a
	atf_check -s eq:1 -o empty -e save:stderr \
	    $(atf_get_srcdir)/h_tools rename a /var/tmp/a
	atf_check -s eq:0 -o ignore -e empty grep "Cross-device link" stderr
	atf_check -s eq:0 -o empty -e empty test -d a
	atf_check -s eq:0 -o empty -e empty rmdir a

	test_unmount
}

atf_test_case basic
basic_head() {
	atf_set "descr" "Tests that basic renames work"
	atf_set "require.user" "root"
}
basic_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir a
	atf_check -s eq:0 -o empty -e empty mv a c
	atf_check -s eq:1 -o empty -e empty test -d a
	atf_check -s eq:0 -o empty -e empty test -d c
	atf_check -s eq:0 -o empty -e empty rmdir c

	test_unmount
}

atf_test_case dotdot
dotdot_head() {
	atf_set "descr" "Tests that the '..' entry is properly updated" \
	                "during moves"
	atf_set "require.user" "root"
}
dotdot_body() {
	test_mount

	echo "Checking if the '..' entry is updated after moves"
	atf_check -s eq:0 -o empty -e empty mkdir a
	atf_check -s eq:0 -o empty -e empty mkdir b
	atf_check -s eq:0 -o empty -e empty mv b a
	atf_check -s eq:0 -o empty -e empty test -d a/b/../b
	atf_check -s eq:0 -o empty -e empty test -d a/b/../../a
	eval $(stat -s a/b)
	[ ${st_nlink} = 2 ] || atf_fail "Incorrect number of links"
	eval $(stat -s a)
	[ ${st_nlink} = 3 ] || atf_fail "Incorrect number of links"
	atf_check -s eq:0 -o empty -e empty rmdir a/b
	atf_check -s eq:0 -o empty -e empty rmdir a

	echo "Checking if the '..' entry is correct after renames"
	atf_check -s eq:0 -o empty -e empty mkdir a
	atf_check -s eq:0 -o empty -e empty mkdir b
	atf_check -s eq:0 -o empty -e empty mv b a
	atf_check -s eq:0 -o empty -e empty mv a c
	atf_check -s eq:0 -o empty -e empty test -d c/b/../b
	atf_check -s eq:0 -o empty -e empty test -d c/b/../../c
	atf_check -s eq:0 -o empty -e empty rmdir c/b
	atf_check -s eq:0 -o empty -e empty rmdir c

	echo "Checking if the '..' entry is correct after multiple moves"
	atf_check -s eq:0 -o empty -e empty mkdir a
	atf_check -s eq:0 -o empty -e empty mkdir b
	atf_check -s eq:0 -o empty -e empty mv b a
	atf_check -s eq:0 -o empty -e empty mv a c
	atf_check -s eq:0 -o empty -e empty mv c/b d
	atf_check -s eq:0 -o empty -e empty test -d d/../c
	atf_check -s eq:0 -o empty -e empty rmdir d
	atf_check -s eq:0 -o empty -e empty rmdir c

	test_unmount
}

atf_test_case dir_to_emptydir
dir_to_emptydir_head() {
	atf_set "descr" "Tests that renaming a directory to override an" \
	                "empty directory works"
	atf_set "require.user" "root"
}
dir_to_emptydir_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir a
	atf_check -s eq:0 -o empty -e empty touch a/c
	atf_check -s eq:0 -o empty -e empty mkdir b
	atf_check -s eq:0 -o empty -e empty \
	    $(atf_get_srcdir)/h_tools rename a b
	atf_check -s eq:1 -o empty -e empty test -e a
	atf_check -s eq:0 -o empty -e empty test -d b
	atf_check -s eq:0 -o empty -e empty test -f b/c
	rm b/c
	rmdir b

	test_unmount
}

atf_test_case dir_to_fulldir
dir_to_fulldir_head() {
	atf_set "descr" "Tests that renaming a directory to override a" \
	                "non-empty directory fails"
	atf_set "require.user" "root"
}
dir_to_fulldir_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir a
	atf_check -s eq:0 -o empty -e empty touch a/c
	atf_check -s eq:0 -o empty -e empty mkdir b
	atf_check -s eq:0 -o empty -e empty touch b/d
	atf_check -s eq:1 -o empty -e save:stderr \
	    $(atf_get_srcdir)/h_tools rename a b
	atf_check -s eq:0 -o ignore -e empty grep "Directory not empty" stderr
	atf_check -s eq:0 -o empty -e empty test -d a
	atf_check -s eq:0 -o empty -e empty test -f a/c
	atf_check -s eq:0 -o empty -e empty test -d b
	atf_check -s eq:0 -o empty -e empty test -f b/d
	rm a/c
	rm b/d
	rmdir a
	rmdir b

	test_unmount
}

atf_test_case dir_to_file
dir_to_file_head() {
	atf_set "descr" "Tests that renaming a directory to override a" \
	                "file fails"
	atf_set "require.user" "root"
}
dir_to_file_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir a
	atf_check -s eq:0 -o empty -e empty touch b
	atf_check -s eq:1 -o empty -e save:stderr \
	    $(atf_get_srcdir)/h_tools rename a b
	atf_check -s eq:0 -o ignore -e empty grep "Not a directory" stderr
	atf_check -s eq:0 -o empty -e empty test -d a
	atf_check -s eq:0 -o empty -e empty test -f b
	rmdir a
	rm b

	test_unmount
}

atf_test_case file_to_dir
file_to_dir_head() {
	atf_set "descr" "Tests that renaming a file to override a" \
	                "directory fails"
	atf_set "require.user" "root"
}
file_to_dir_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty touch a
	atf_check -s eq:0 -o empty -e empty mkdir b
	atf_check -s eq:1 -o empty -e save:stderr \
	    $(atf_get_srcdir)/h_tools rename a b
	atf_check -s eq:0 -o ignore -e empty grep "Is a directory" stderr
	atf_check -s eq:0 -o empty -e empty test -f a
	atf_check -s eq:0 -o empty -e empty test -d b
	rm a
	rmdir b

	test_unmount
}

atf_test_case kqueue
kqueue_head() {
	atf_set "descr" "Tests that moving and renaming files raise the" \
	                "correct kqueue events"
	atf_set "require.user" "root"
}
kqueue_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir dir
	atf_check -s eq:0 -o empty -e empty touch dir/a
	echo 'mv dir/a dir/b' | kqueue_monitor 2 dir dir/a
	kqueue_check dir/a NOTE_RENAME
	kqueue_check dir NOTE_WRITE
	atf_check -s eq:0 -o empty -e empty rm dir/b
	atf_check -s eq:0 -o empty -e empty rmdir dir

	atf_check -s eq:0 -o empty -e empty mkdir dir
	atf_check -s eq:0 -o empty -e empty touch dir/a
	atf_check -s eq:0 -o empty -e empty touch dir/b
	echo 'mv dir/a dir/b' | kqueue_monitor 3 dir dir/a dir/b
	kqueue_check dir/a NOTE_RENAME
	kqueue_check dir NOTE_WRITE
	kqueue_check dir/b NOTE_DELETE
	atf_check -s eq:0 -o empty -e empty rm dir/b
	atf_check -s eq:0 -o empty -e empty rmdir dir

	atf_check -s eq:0 -o empty -e empty mkdir dir1
	atf_check -s eq:0 -o empty -e empty mkdir dir2
	atf_check -s eq:0 -o empty -e empty touch dir1/a
	echo 'mv dir1/a dir2/a' | kqueue_monitor 3 dir1 dir1/a dir2
	kqueue_check dir1/a NOTE_RENAME
	kqueue_check dir1 NOTE_WRITE
	kqueue_check dir2 NOTE_WRITE
	atf_check -s eq:0 -o empty -e empty rm dir2/a
	atf_check -s eq:0 -o empty -e empty rmdir dir1
	atf_check -s eq:0 -o empty -e empty rmdir dir2

	test_unmount
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case dots
	atf_add_test_case crossdev
	atf_add_test_case basic
	atf_add_test_case dotdot
	atf_add_test_case dir_to_emptydir
	atf_add_test_case dir_to_fulldir
	atf_add_test_case dir_to_file
	atf_add_test_case file_to_dir
	atf_add_test_case kqueue
}
