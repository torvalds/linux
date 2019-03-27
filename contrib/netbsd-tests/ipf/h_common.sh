# $NetBSD: h_common.sh,v 1.8 2013/05/16 07:20:29 martin Exp $
#
# Copyright (c) 2008, 2010 The NetBSD Foundation, Inc.
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
# (C)opyright 1993-1996 by Darren Reed.
#
# See the IPFILTER.LICENCE file for details on licencing.
#

h_copydata()
{
	test -f $(atf_get_srcdir)/input/$1 && \
	    cp $(atf_get_srcdir)/input/$1 in
	test -f $(atf_get_srcdir)/regress/$1 && \
	    cp $(atf_get_srcdir)/regress/$1 reg
	test -f $(atf_get_srcdir)/expected/$1 && \
	    cp $(atf_get_srcdir)/expected/$1 exp
}

test_case()
{
	local name="${1}"; shift
	local check_function="${1}"; shift

	atf_test_case "${name}"
	eval "${name}_body() { \
		${check_function} '${name}' " "${@}" "; \
	}"
}

broken_test_case()
{
	local name="${1}"; shift
	local check_function="${1}"; shift

	atf_test_case "${name}"
	eval "${name}_body() { \
		atf_skip 'This test case is probably broken'; \
		${check_function} '${name}' " "${@}" "; \
	}"
}

failing_test_case()
{
	local name="${1}"; shift
	local check_function="${1}"; shift
	local reason="${1}"; shift

	atf_test_case "${name}"
	eval "${name}_body() { \
		atf_expect_fail '${reason}'; \
		${check_function} '${name}' " "${@}" "; \
	}"
}

failing_test_case_be()
{
	# this test fails on some architectures - not fully analyzed, assume
	# an endianess bug
	local name="${1}"; shift
	local check_function="${1}"; shift
	local reason="${1}"; shift

	atf_test_case "${name}"

	if [ `sysctl -n hw.byteorder` = 4321 ]; then
		eval "${name}_body() { \
			atf_expect_fail '${reason}'; \
			${check_function} '${name}' " "${@}" "; \
		}"
	else
		eval "${name}_body() { \
			${check_function} '${name}' " "${@}" "; \
		}"
	fi
}

