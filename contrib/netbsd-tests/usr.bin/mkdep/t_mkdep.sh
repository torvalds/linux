# $NetBSD: t_mkdep.sh,v 1.4 2012/08/26 22:37:19 jmmv Exp $
#
# Copyright (c) 2011 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Nicolas Joly.
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

atf_test_case prefix
prefix_head() {
	atf_set "descr" "Test adding a prefix to a single target"
	atf_set "require.progs" "mkdep cc"
}
prefix_body() {

	atf_check touch sample.c

	atf_check mkdep -f sample.d -P some/path/ sample.c
	atf_check -o ignore grep '^some/path/sample.o:' sample.d
}

atf_test_case suffixes
suffixes_head() {
	atf_set "descr" "Test suffixes list"
	atf_set "require.progs" "mkdep cc"
}
suffixes_body() {

	atf_check touch sample.c

	# No list
	atf_check mkdep -f sample.d sample.c
	atf_check -o ignore grep '^sample.o:' sample.d

	# Suffix list
	atf_check mkdep -f sample.d -s '.a .b' sample.c
	atf_check -o ignore grep '^sample.b sample.a:' sample.d

	# Empty list
	atf_check mkdep -f sample.d -s '' sample.c
	atf_check -o ignore grep '^sample:' sample.d
}

atf_test_case prefix_and_suffixes
prefix_and_suffixes_head() {
	atf_set "descr" "Test the combination of a prefix and suffixes"
	atf_set "require.progs" "mkdep cc"
}
prefix_and_suffixes_body() {

	atf_check touch sample.c

	atf_check mkdep -f sample.d -s '.a .b' -P c/d sample.c
	atf_check -o ignore grep '^c/dsample.b c/dsample.a:' sample.d
}

atf_init_test_cases() {
	atf_add_test_case prefix
	atf_add_test_case suffixes
	atf_add_test_case prefix_and_suffixes
}
