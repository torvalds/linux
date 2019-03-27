# $NetBSD: t_diff.sh,v 1.3 2012/03/13 05:40:00 jruoho Exp $
#
# Copyright (c) 2011 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Jukka Ruohonen.
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

atf_test_case mallocv
mallocv_head() {
	atf_set "descr" "Test diff(1) with MALLOC_OPTIONS=V (cf. PR bin/26453)"
}

mallocv_body() {

	atf_check -s ignore \
		-e not-inline:"diff: memory exhausted\n" \
		-x "env MALLOC_OPTIONS=V diff " \
		   "$(atf_get_srcdir)/d_mallocv1.in" \
		   "$(atf_get_srcdir)/d_mallocv2.in"
}

atf_test_case nomallocv
nomallocv_head() {
	atf_set "descr" "Test diff(1) with no MALLOC_OPTIONS=V"
}

nomallocv_body() {

	atf_check -s exit:0 \
		-e inline:"" \
		-x "diff " \
		   "$(atf_get_srcdir)/d_mallocv1.in" \
		   "$(atf_get_srcdir)/d_mallocv2.in"
}

atf_test_case same
same_head() {
	atf_set "descr" "Test diff(1) with identical files"
}

same_body() {

	atf_check -s exit:0 \
		-e inline:"" \
		-x "diff $(atf_get_srcdir)/t_diff $(atf_get_srcdir)/t_diff"
}

atf_init_test_cases() {
	atf_add_test_case mallocv
	atf_add_test_case nomallocv
	atf_add_test_case same
}
