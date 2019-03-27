# $NetBSD: t_link.sh,v 1.5 2010/11/07 17:51:18 jmmv Exp $
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
# Verifies that the link operation works.
#

atf_test_case basic
basic_head() {
	atf_set "descr" "Verifies that the link operation works on files" \
	                "at the top directory"
	atf_set "require.user" "root"
}
basic_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty touch a
	atf_check -s eq:0 -o empty -e empty touch z
	eval $(stat -s a | sed -e 's|st_|sta_|g')
	eval $(stat -s z | sed -e 's|st_|stz_|g')
	test ${sta_ino} != ${stz_ino} || \
	    atf_fail "Node numbers are not different"
	test ${sta_nlink} -eq 1 || atf_fail "Number of links is incorrect"
	atf_check -s eq:0 -o empty -e empty ln a b

	echo "Checking if link count is correct after links are created"
	eval $(stat -s a | sed -e 's|st_|sta_|g')
	eval $(stat -s b | sed -e 's|st_|stb_|g')
	test ${sta_ino} = ${stb_ino} || atf_fail "Node number changed"
	test ${sta_nlink} -eq 2 || atf_fail "Link count is incorrect"
	test ${stb_nlink} -eq 2 || atf_fail "Link count is incorrect"

	echo "Checking if link count is correct after links are deleted"
	atf_check -s eq:0 -o empty -e empty rm a
	eval $(stat -s b | sed -e 's|st_|stb_|g')
	test ${stb_nlink} -eq 1 || atf_fail "Link count is incorrect"
	atf_check -s eq:0 -o empty -e empty rm b

	test_unmount
}

atf_test_case subdirs
subdirs_head() {
	atf_set "descr" "Verifies that the link operation works if used" \
	                "in subdirectories"
	atf_set "require.user" "root"
}
subdirs_body() {
	test_mount

	atf_check -s eq:0 -o empty -e empty touch a
	atf_check -s eq:0 -o empty -e empty mkdir c
	atf_check -s eq:0 -o empty -e empty ln a c/b

	echo "Checking if link count is correct after links are created"
	eval $(stat -s a | sed -e 's|st_|sta_|g')
	eval $(stat -s c/b | sed -e 's|st_|stb_|g')
	test ${sta_ino} = ${stb_ino} || atf_fail "Node number changed"
	test ${sta_nlink} -eq 2 || atf_fail "Link count is incorrect"
	test ${stb_nlink} -eq 2 || atf_fail "Link count is incorrect"

	echo "Checking if link count is correct after links are deleted"
	atf_check -s eq:0 -o empty -e empty rm a
	eval $(stat -s c/b | sed -e 's|st_|stb_|g')
	test ${stb_nlink} -eq 1 || atf_fail "Link count is incorrect"
	atf_check -s eq:0 -o empty -e empty rm c/b
	atf_check -s eq:0 -o empty -e empty rmdir c

	test_unmount
}

# Begin FreeBSD
if true; then
atf_test_case kqueue cleanup
kqueue_cleanup() {
	Mount_Point=$(pwd)/mntpt _test_unmount || :
}
else
# End FreeBSD
atf_test_case kqueue
# Begin FreeBSD
fi
# End FreeBSD
kqueue_head() {
	atf_set "descr" "Verifies that creating a link raises the correct" \
	                "kqueue events"
	atf_set "require.user" "root"
}
kqueue_body() {
	test_mount

	# Begin FreeBSD
	atf_expect_fail "fails with: dir/b did not receive NOTE_LINK - bug 213662"
	# End FreeBSD

	atf_check -s eq:0 -o empty -e empty mkdir dir
	atf_check -s eq:0 -o empty -e empty touch dir/a
	echo 'ln dir/a dir/b' | kqueue_monitor 2 dir dir/a
	kqueue_check dir/a NOTE_LINK
	kqueue_check dir NOTE_WRITE

	echo 'rm dir/a' | kqueue_monitor 2 dir dir/b
	# XXX According to the (short) kqueue(2) documentation, the following
	# should raise a NOTE_LINK but FFS raises a NOTE_DELETE...
	kqueue_check dir/b NOTE_LINK
	kqueue_check dir NOTE_WRITE
	atf_check -s eq:0 -o empty -e empty rm dir/b
	atf_check -s eq:0 -o empty -e empty rmdir dir

	test_unmount
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case basic
	atf_add_test_case subdirs
	atf_add_test_case kqueue
}
