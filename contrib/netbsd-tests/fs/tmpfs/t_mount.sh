# $NetBSD: t_mount.sh,v 1.6 2010/11/07 17:51:18 jmmv Exp $
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
# Verifies that an execution of mount and umount works correctly without
# causing errors and that the root node gets correct attributes.
# Also verifies command line parsing from mount_tmpfs.
#

atf_test_case plain
plain_head() {
	atf_set "descr" "Tests a mount and unmount without any options"
	atf_set "require.user" "root"
}
plain_body() {
	test_mount
	test_unmount
}

atf_test_case links
links_head() {
	atf_set "descr" "Tests that the mount point has two hard links"
	atf_set "require.user" "root"
}
links_body() {
	test_mount
	eval $(stat -s ${Mount_Point})
	[ ${st_nlink} = 2 ] || \
	    atf_fail "Root directory does not have two hard links"
	test_unmount
}

atf_test_case options
options_head() {
	atf_set "descr" "Tests the read-only mount option"
	atf_set "require.user" "root"
}
options_body() {
	test_mount -o ro
	mount | grep ${Mount_Point} | grep -q read-only || \
	    atf_fail "read-only option (ro) does not work"
	test_unmount
}

atf_test_case attrs
attrs_head() {
	atf_set "descr" "Tests that root directory attributes are set" \
	                "correctly"
	atf_set "require.user" "root"
}
attrs_body() {
	test_mount -o -u1000 -o -g100 -o -m755
	eval $(stat -s ${Mount_Point})
	[ ${st_uid} = 1000 ] || atf_fail "uid is incorrect"
	[ ${st_gid} = 100 ] || atf_fail "gid is incorrect"
	[ ${st_mode} = 040755 ] || atf_fail "mode is incorrect"
	test_unmount
}

atf_test_case negative
negative_head() {
	atf_set "descr" "Tests that negative values passed to to -s are" \
	                "handled correctly"
	atf_set "require.user" "root"
}
negative_body() {
	mkdir tmp
	test_mount -o -s-10
	test_unmount
}

# Begin FreeBSD
if true; then
atf_test_case large cleanup
large_cleanup() {
	umount -f tmp 2>/dev/null || :
	Mount_Point=$(pwd)/mnt _test_unmount || :
}
else
# End FreeBSD
atf_test_case large
# Begin FreeBSD
fi
# End FreeBSD
large_head() {
	atf_set "descr" "Tests that extremely long values passed to -s" \
	                "are handled correctly"
	atf_set "require.user" "root"
}
large_body() {
	test_mount -o -s9223372036854775807
	test_unmount

	# Begin FreeBSD
	atf_expect_fail "-o -s<large-size> succeeds unexpectedly on FreeBSD - bug 212862"
	# End FreeBSD

	mkdir tmp
	atf_check -s eq:1 -o empty -e ignore \
	    mount -t tmpfs -o -s9223372036854775808 tmpfs tmp
	atf_check -s eq:1 -o empty -e ignore \
	    mount -t tmpfs -o -s9223372036854775808g tmpfs tmp
	rmdir tmp
}

atf_test_case mntpt
mntpt_head() {
	atf_set "descr" "Tests that the error messages printed when the" \
	                "mount point is invalid do not show the source" \
	                "unused parameter"
}
mntpt_body() {
	mount_tmpfs unused $(pwd)/mnt >out 2>&1
	atf_check -s eq:1 -o empty -e empty grep unused out
	atf_check -s eq:0 -o ignore -e empty grep "$(pwd)/mnt" out

	mount_tmpfs unused mnt >out 2>&1
	atf_check -s eq:1 -o empty -e empty grep unused out
	atf_check -s eq:0 -o ignore -e empty grep mnt out
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case plain
	atf_add_test_case options
	atf_add_test_case attrs
	atf_add_test_case negative
	atf_add_test_case large
	atf_add_test_case mntpt
}
