# $NetBSD: t_fdpass.sh,v 1.2 2012/08/16 08:39:43 martin Exp $
#
# Copyright (c) 2012 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Christos Zoulas
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

have32() {
	local src="$(atf_get_srcdir)"
	if cmp "${src}/fdpass64" "${src}/fdpass32" > /dev/null
	then
		# echo "no -m32 support"
		return 1
	else
		return 0
	fi
}

atf_test_case fdpass_normal

fdpass_normal_head() {
	atf_set "descr" "Test file descriptor passing (default)"
}

fdpass_normal_body() {
	local src="$(atf_get_srcdir)"
	atf_check "${src}/fdpass64"
}


atf_test_case fdpass_compat

fdpass_compat_head() {
	atf_set "descr" "Test file descriptor passing (compat)"
}

fdpass_compat_body() {
	local src="$(atf_get_srcdir)"
	have32 && atf_check "${src}/fdpass32"
}


atf_test_case fdpass_normal_compat

fdpass_normal_compat_head() {
	atf_set "descr" "Test file descriptor passing (normal->compat)"
}

fdpass_normal_compat_body() {
	local src="$(atf_get_srcdir)"
	have32 && atf_check "${src}/fdpass64" -p "${src}/fdpass32"
}


atf_test_case fdpass_compat_normal

fdpass_compat_normal_head() {
	atf_set "descr" "Test file descriptor passing (normal->compat)"
}

fdpass_compat_normal_body() {
	local src="$(atf_get_srcdir)"
	have32 && atf_check "${src}/fdpass32" -p "${src}/fdpass64"
}


atf_init_test_cases()
{
	atf_add_test_case fdpass_normal
	if have32
	then
		atf_add_test_case fdpass_compat
		atf_add_test_case fdpass_compat_normal
		atf_add_test_case fdpass_normal_compat
	fi
}
