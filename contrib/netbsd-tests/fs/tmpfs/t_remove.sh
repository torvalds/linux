# $NetBSD: t_remove.sh,v 1.5 2010/11/07 17:51:18 jmmv Exp $
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
# Verifies that the remove operation works.
#

atf_test_case single
single_head() {
	atf_set "descr" "Checks that file removal works"
	atf_set "require.user" "root"
}
single_body() {
	test_mount

	atf_check -s eq:1 -o empty -e empty test -f a
	atf_check -s eq:0 -o empty -e empty touch a
	atf_check -s eq:0 -o empty -e empty test -f a
	atf_check -s eq:0 -o empty -e empty rm a
	atf_check -s eq:1 -o empty -e empty test -f a

	test_unmount
}

# Begin FreeBSD
if true; then
atf_test_case uchg cleanup
uchg_cleanup() {
	Mount_Point=$(pwd)/mntpt _test_unmount
}
else
# End FreeBSD
atf_test_case uchg
# Begin FreeBSD
fi
# End FreeBSD
uchg_head() {
	atf_set "descr" "Checks that files with the uchg flag set cannot" \
	                "be removed"
	atf_set "require.user" "root"
}
uchg_body() {
	# Begin FreeBSD
	atf_expect_fail "this fails on FreeBSD with root - bug 212861"
	# End FreeBSD

	test_mount

	atf_check -s eq:0 -o empty -e empty touch a
	atf_check -s eq:0 -o empty -e empty chflags uchg a
	atf_check -s eq:1 -o empty -e ignore rm -f a
	atf_check -s eq:0 -o empty -e empty chflags nouchg a
	atf_check -s eq:0 -o empty -e empty rm a
	atf_check -s eq:1 -o empty -e empty test -f a

	test_unmount
}

atf_test_case dot
dot_head() {
	atf_set "descr" "Checks that '.' cannot be removed"
	atf_set "require.user" "root"
}
dot_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir a
	atf_check -s eq:1 -o empty -e ignore unlink a/.
	atf_check -s eq:0 -o empty -e empty rmdir a

	test_unmount
}

atf_test_case kqueue
kqueue_head() {
	atf_set "descr" "Removes a file and checks the kqueue events" \
	                "raised"
	atf_set "require.user" "root"
}
kqueue_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty mkdir dir
	atf_check -s eq:0 -o empty -e empty touch dir/a
	echo 'rm dir/a' | kqueue_monitor 2 dir dir/a
	kqueue_check dir/a NOTE_DELETE
	kqueue_check dir NOTE_WRITE
	atf_check -s eq:0 -o empty -e empty rmdir dir

	test_unmount
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case single
	atf_add_test_case uchg
	atf_add_test_case dot
	atf_add_test_case kqueue
}
