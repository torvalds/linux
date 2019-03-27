# $NetBSD: t_statvfs.sh,v 1.4 2010/11/07 17:51:18 jmmv Exp $
#
# Copyright (c) 2005, 2006, 2007 The NetBSD Foundation, Inc.
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
# Verifies that the statvfs system call works properly (returning the
# correct values) over a tmpfs mount point.
#

atf_test_case values
values_head() {
	atf_set "descr" "Tests that statvfs(2) returns correct values"
	atf_set "require.user" "root"
}
values_body() {
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
	eval $($(atf_get_srcdir)/h_tools statvfs .)
	[ ${pagesize} -eq ${f_bsize} ] || \
	    atf_fail "Invalid bsize"
	[ $((${f_bsize} * ${f_blocks})) -ge $((10 * 1024 * 1024)) ] || \
	    atf_file "bsize * blocks too small"
	[ $((${f_bsize} * ${f_blocks})) -le \
	    $((10 * 1024 * 1024 + ${pagesize})) ] || \
	    atf_fail "bsize * blocks too big"

	test_unmount
}

atf_init_test_cases() {
	. $(atf_get_srcdir)/../h_funcs.subr
	. $(atf_get_srcdir)/h_funcs.subr

	atf_add_test_case values
}
