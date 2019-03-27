# Copyright 2012 Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
# * Neither the name of Google Inc. nor the names of its contributors
#   may be used to endorse or promote products derived from this software
#   without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Helper function for the various one_* test cases.
#
# The first argument must be one of C, CXX or SH, and this indicates the
# language of the test program.
#
# The second argument is the name of the test program, without an extension.
# The corresponding source file must exist in the current directory.
one_test() {

	if [ ! -e /usr/bin/gcc -a -e /usr/bin/clang ]; then
		export HAVE_LLVM=yes
	fi
	local lang="${1}"; shift
	local name="${1}"; shift

	cat >Makefile <<EOF
.include <bsd.own.mk>
TESTSDIR = \${TESTSBASE}/fake
TESTS_${lang} = ${name}
.include <bsd.test.mk>
EOF

	atf_check -o ignore make
	mkdir -p root/usr/tests/fake
	create_make_conf mk.conf owngrp DESTDIR="$(pwd)/root"
	atf_check -o ignore make install

	atf_check -o match:'ident: one_tc' "./root/usr/tests/fake/${name}" -l
}

atf_test_case one_c
one_c_body() {
	cat >t_fake.c <<EOF
#include <atf-c.h>
ATF_TC_WITHOUT_HEAD(one_tc);
ATF_TC_BODY(one_tc, tc)
{
	atf_tc_fail("Failing explicitly");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, one_tc);
	return atf_no_error();
}
EOF
	one_test C t_fake
}

atf_test_case one_cxx
one_cxx_body() {
	cat >t_fake.cpp <<EOF
#include <atf-c++.hpp>
ATF_TEST_CASE_WITHOUT_HEAD(one_tc);
ATF_TEST_CASE_BODY(one_tc)
{
	fail("Failing explicitly");
}

ATF_INIT_TEST_CASES(tcs)
{
	ATF_ADD_TEST_CASE(tcs, one_tc);
}
EOF
	one_test CXX t_fake
}

atf_test_case one_sh
one_sh_body() {
	cat >t_fake.sh <<EOF
atf_test_case one_tc
one_tc_body() {
	atf_fail "Failing explicitly"
}

atf_init_test_cases() {
	atf_add_test_case one_tc
}
EOF
	one_test SH t_fake
}

atf_init_test_cases() {
	atf_add_test_case one_c
	atf_add_test_case one_cxx
	atf_add_test_case one_sh
}
