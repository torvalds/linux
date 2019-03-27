# $NetBSD: t_sizes.sh,v 1.5 2010/11/07 17:51:18 jmmv Exp $
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
# Verifies that the file system controls memory usage correctly.
#

atf_test_case small
small_head() {
	atf_set "descr" "Checks the status after creating a small file"
	atf_set "require.user" "root"
}
small_body() {
	test_mount -o -s10M

	echo a >a || atf_fail "Could not create file"
	eval $($(atf_get_srcdir)/h_tools statvfs .)
	f_bused=$((${f_blocks} - ${f_bfree}))
	[ ${f_bused} -gt 1 ] || atf_fail "Incorrect bused count"
	atf_check -s eq:0 -o empty -e empty rm a

	test_unmount
}

atf_test_case big
big_head() {
	atf_set "descr" "Checks the status after creating a big file"
	atf_set "require.user" "root"
}
big_body() {
	test_mount -o -s10M

	# Begin FreeBSD
	if true; then
		pagesize=$(sysctl -n hw.pagesize)
	else
	# End FreeBSD
	pagesize=$(sysctl hw.pagesize | cut -d ' ' -f 3)
	# Begin FreeBSD
	fi
	# End FreeBSD
	eval $($(atf_get_srcdir)/h_tools statvfs . | sed -e 's|^f_|cf_|')
	cf_bused=$((${cf_blocks} - ${cf_bfree}))

	atf_check -s eq:0 -o ignore -e ignore \
	    dd if=/dev/zero of=a bs=1m count=5
	eval $($(atf_get_srcdir)/h_tools statvfs .)
	f_bused=$((${f_blocks} - ${f_bfree}))
	[ ${f_bused} -ne ${cf_bused} ] || atf_fail "bused did not change"
	[ ${f_bused} -gt $((5 * 1024 * 1024 / ${pagesize})) ] || \
	    atf_fail "bused too big"
	of_bused=${f_bused}
	atf_check -s eq:0 -o empty -e empty rm a
	eval $($(atf_get_srcdir)/h_tools statvfs .)
	f_bused=$((${f_blocks} - ${f_bfree}))
	[ ${f_bused} -lt ${of_bused} ] || \
	    atf_fail "bused was not correctly restored"

	test_unmount
}

atf_test_case overflow
overflow_head() {
	atf_set "descr" "Checks the status after creating a big file that" \
	                "overflows the file system limits"
	atf_set "require.user" "root"
}
overflow_body() {
	test_mount -o -s10M

	atf_check -s eq:0 -o empty -e empty touch a
	atf_check -s eq:0 -o empty -e empty rm a
	eval $($(atf_get_srcdir)/h_tools statvfs .)
	of_bused=$((${f_blocks} - ${f_bfree}))
	atf_check -s eq:1 -o ignore -e ignore \
	    dd if=/dev/zero of=a bs=1m count=15
	atf_check -s eq:0 -o empty -e empty rm a
	eval $($(atf_get_srcdir)/h_tools statvfs .)
	f_bused=$((${f_blocks} - ${f_bfree}))
	[ ${f_bused} -ge ${of_bused} -a ${f_bused} -le $((${of_bused} + 1)) ] \
	    || atf_fail "Incorrect bused"

	test_unmount
}

atf_test_case overwrite
overwrite_head() {
	atf_set "descr" "Checks that writing to the middle of a file" \
	                "does not change its size"
	atf_set "require.user" "root"
}
overwrite_body() {
	test_mount -o -s10M

	atf_check -s eq:0 -o ignore -e ignore \
	    dd if=/dev/zero of=a bs=1024 count=10
	sync
	atf_check -s eq:0 -o ignore -e ignore \
	    dd if=/dev/zero of=a bs=1024 conv=notrunc seek=1 count=1
	sync
	eval $(stat -s a)
	[ ${st_size} -eq 10240 ] || atf_fail "Incorrect file size"

	test_unmount
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case small
	atf_add_test_case big
	atf_add_test_case overflow
	atf_add_test_case overwrite
}
