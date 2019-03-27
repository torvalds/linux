# $NetBSD: t_sleep.sh,v 1.1 2012/03/30 09:27:10 jruoho Exp $
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

atf_test_case fraction
fraction_head() {
	atf_set "descr" "Test that sleep(1) handles " \
			"fractions of a second (PR bin/3914)"
}

fraction_body() {

	atf_check -s exit:0 -o empty -e empty -x "sleep 0.1"
	atf_check -s exit:0 -o empty -e empty -x "sleep 0.2"
	atf_check -s exit:0 -o empty -e empty -x "sleep 0.3"
}

atf_test_case hex
hex_head() {
	atf_set "descr" "Test that sleep(1) handles hexadecimal arguments"
}

hex_body() {

	atf_check -s exit:0 -o empty -e empty -x "sleep 0x01"
}

atf_test_case nonnumeric
nonnumeric_head() {
	atf_set "descr" "Test that sleep(1) errors out with " \
			"non-numeric argument (PR bin/27140)"
}

nonnumeric_body() {

	atf_check -s not-exit:0 -o empty -e not-empty -x "sleep xyz"
	atf_check -s not-exit:0 -o empty -e not-empty -x "sleep x21"
	atf_check -s not-exit:0 -o empty -e not-empty -x "sleep  /3"
}

atf_init_test_cases() {

	atf_add_test_case fraction
	atf_add_test_case hex
	atf_add_test_case nonnumeric
}
