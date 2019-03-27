# $NetBSD: t_factor.sh,v 1.9 2016/06/27 05:29:32 pgoyette Exp $
#
# Copyright (c) 2007, 2008, 2009 The NetBSD Foundation, Inc.
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

expect() {
	echo "${2}" >expout
	ncrypt=$( ldd /usr/games/factor | grep -c -- -lcrypt )
	if [ "X$3" != "X" -a $ncrypt -eq 0 ] ; then 
		atf_skip "crypto needed for huge non-prime factors - PR bin/23663"
	fi
	atf_check -s eq:0 -o file:expout -e empty /usr/games/factor ${1}
}

atf_test_case overflow1
overflow1_head() {
	atf_set "descr" "Tests for overflow conditions"
	atf_set "require.progs" "/usr/games/factor"
}
overflow1_body() {
	expect '8675309' '8675309: 8675309'
}

atf_test_case overflow2
overflow2_head() {
	atf_set "descr" "Tests for overflow conditions"
	atf_set "require.progs" "/usr/games/factor"
}
overflow2_body() {
	expect '6172538568' '6172538568: 2 2 2 3 7 17 2161253'
}

atf_test_case loop1
loop1_head() {
	atf_set "descr" "Tests some cases that once locked the program" \
	                "in an infinite loop"
	atf_set "require.progs" "/usr/games/factor"
}
loop1_body() {
	expect '2147483647111311' '2147483647111311: 3 3 3 131 607148331103'
}

atf_test_case loop2
loop2_head() {
	atf_set "descr" "Tests some cases that once locked the program" \
	                "in an infinite loop"
	atf_set "require.progs" "/usr/games/factor"
}
loop2_body() {
	expect '99999999999991' '99999999999991: 7 13 769231 1428571' Need_Crypto
}

atf_init_test_cases()
{
	atf_add_test_case overflow1
	atf_add_test_case overflow2
	atf_add_test_case loop1
	atf_add_test_case loop2
}
