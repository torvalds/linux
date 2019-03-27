# $NetBSD: t_nat_parse.sh,v 1.8 2014/06/29 09:26:32 darrenr Exp $
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

intest()
{
	h_copydata $1

	atf_check -o file:exp -e ignore ipnat -Rnvf reg
}

test_case in1 intest text text
test_case in2 intest text text
test_case in3 intest text text
test_case in4 intest text text
test_case in5 intest text text
test_case in6 intest text text
test_case in7 intest text text
test_case in100 intest text text
test_case in101 intest text text
test_case in102 intest text text
test_case in1_6 intest text text
test_case in2_6 intest text text
test_case in3_6 intest text text
test_case in4_6 intest text text
test_case in5_6 intest text text
test_case in6_6 intest text text
test_case in8_6 intest text text
test_case in100_6 intest text text
test_case in101_6 intest text text
test_case in102_6 intest text text

atf_init_test_cases()
{
	atf_add_test_case in1
	atf_add_test_case in2
	atf_add_test_case in3
	atf_add_test_case in4
	atf_add_test_case in5
	atf_add_test_case in6
	atf_add_test_case in7
	atf_add_test_case in100
	atf_add_test_case in101
	atf_add_test_case in102
	atf_add_test_case in1_6
	atf_add_test_case in2_6
	atf_add_test_case in3_6
	atf_add_test_case in4_6
	atf_add_test_case in5_6
	atf_add_test_case in6_6
	atf_add_test_case in8_6
	atf_add_test_case in100_6
	atf_add_test_case in101_6
	atf_add_test_case in102_6
}
