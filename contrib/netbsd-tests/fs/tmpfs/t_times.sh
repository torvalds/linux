# $NetBSD: t_times.sh,v 1.5 2010/11/07 17:51:18 jmmv Exp $
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
# Verifies that node times are properly handled.
#

atf_test_case empty
empty_head() {
	atf_set "descr" "Tests that creating an empty file and later" \
	                "manipulating it updates times correctly"
	atf_set "require.user" "root"
}
empty_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty touch a
	eval $(stat -s a | sed -e 's|st_|ost_|g') || atf_fail "stat failed"
	[ ${ost_birthtime} -eq ${ost_atime} ] || atf_fail "Incorrect atime"
	[ ${ost_birthtime} -eq ${ost_ctime} ] || atf_fail "Incorrect ctime"
	[ ${ost_birthtime} -eq ${ost_mtime} ] || atf_fail "Incorrect mtime"

	sleep 1
	atf_check -s eq:0 -o ignore -e empty cat a
	eval $(stat -s a) || atf_fail "stat failed"
	[ ${st_atime} -gt ${ost_atime} ] || atf_fail "Incorrect atime"
	[ ${st_ctime} -eq ${ost_ctime} ] || atf_fail "Incorrect ctime"
	[ ${st_mtime} -eq ${ost_mtime} ] || atf_fail "Incorrect mtime"

	sleep 1
	echo foo >a || atf_fail "Write failed"
	eval $(stat -s a) || atf_fail "stat failed"
	[ ${st_atime} -gt ${ost_atime} ] || atf_fail "Incorrect atime"
	[ ${st_ctime} -gt ${ost_ctime} ] || atf_fail "Incorrect ctime"
	[ ${st_mtime} -gt ${ost_mtime} ] || atf_fail "Incorrect mtime"

	test_unmount
}

atf_test_case non_empty
non_empty_head() {
	atf_set "descr" "Tests that creating a non-empty file and later" \
	                "manipulating it updates times correctly"
	atf_set "require.user" "root"
}
non_empty_body() {
	test_mount

	echo foo >b || atf_fail "Non-empty creation failed"
	eval $(stat -s b | sed -e 's|st_|ost_|g') || atf_fail "stat failed"

	sleep 1
	atf_check -s eq:0 -o ignore -e empty cat b
	eval $(stat -s b) || atf_fail "stat failed"
	[ ${st_atime} -gt ${ost_atime} ] || atf_fail "Incorrect atime"
	[ ${st_ctime} -eq ${ost_ctime} ] || atf_fail "Incorrect ctime"
	[ ${st_mtime} -eq ${ost_mtime} ] || atf_fail "Incorrect mtime"

	test_unmount
}

atf_test_case link
link_head() {
	atf_set "descr" "Tests that linking to an existing file updates" \
	                "times correctly"
	atf_set "require.user" "root"
}
link_body() {
	test_mount

	echo foo >c || atf_fail "Non-empty creation failed"
	eval $(stat -s c | sed -e 's|st_|ost_|g') || atf_fail "stat failed"

	sleep 1
	atf_check -s eq:0 -o empty -e empty ln c d
	eval $(stat -s c) || atf_fail "stat failed"
	[ ${st_atime} -eq ${ost_atime} ] || atf_fail "Incorrect atime"
	[ ${st_ctime} -gt ${ost_ctime} ] || atf_fail "Incorrect ctime"
	[ ${st_mtime} -eq ${ost_mtime} ] || atf_fail "Incorrect mtime"

	test_unmount
}

atf_test_case rename
rename_head() {
	atf_set "descr" "Tests that renaming an existing file updates" \
	                "times correctly"
	atf_set "require.user" "root"
}
rename_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir e
	echo foo >e/a || atf_fail "Creation failed"
	eval $(stat -s e | sed -e 's|st_|dost_|g') || atf_fail "stat failed"
	eval $(stat -s e/a | sed -e 's|st_|ost_|g') || atf_fail "stat failed"
	sleep 1
	atf_check -s eq:0 -o empty -e empty mv e/a e/b
	eval $(stat -s e | sed -e 's|st_|dst_|g') || atf_fail "stat failed"
	eval $(stat -s e/b) || atf_fail "stat failed"
	[ ${st_atime} -eq ${ost_atime} ] || atf_fail "Incorrect atime"
	[ ${st_ctime} -gt ${ost_ctime} ] || atf_fail "Incorrect ctime"
	[ ${st_mtime} -eq ${ost_mtime} ] || atf_fail "Incorrect mtime"
	[ ${dst_mtime} -gt ${dost_mtime} ] || atf_fail "Incorrect mtime"

	test_unmount
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case empty
	atf_add_test_case non_empty
	atf_add_test_case link
	atf_add_test_case rename
}
