# $NetBSD: t_cmp.sh,v 1.1 2012/03/19 07:05:18 jruoho Exp $
#
# Copyright (c) 2012 The NetBSD Foundation, Inc.
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

atf_test_case missing
missing_head() {
	atf_set "descr" "Test that cmp(1) with '-s' is silent " \
			"when files are missing (PR bin/2642)"
}

missing_body() {

	echo a > a

	atf_check -s not-exit:0 -o empty -e empty -x "cmp -s a x"
	atf_check -s not-exit:0 -o empty -e empty -x "cmp -s x a"
	atf_check -s not-exit:0 -o empty -e empty -x "cmp -s x y"
	atf_check -s not-exit:0 -o empty -e empty -x "cmp -s y x"
}

atf_test_case skip
skip_head() {
	atf_set "descr" "Test that cmp(1) handles skip " \
			"parameters correctly (PR bin/23836)"
}

skip_body() {

	echo 0123456789abcdef > a
	echo abcdef > b

	atf_check -s exit:0 -o empty -e empty -x "cmp a b '10'"
	atf_check -s exit:0 -o empty -e empty -x "cmp a b '0xa'"
	atf_check -s exit:1 -o not-empty -e empty -x "cmp a b '9'"
}

atf_init_test_cases()
{
	atf_add_test_case missing
	atf_add_test_case skip
}
