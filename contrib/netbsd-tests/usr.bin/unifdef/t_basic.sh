# $NetBSD: t_basic.sh,v 1.6 2012/10/15 17:49:58 njoly Exp $
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

atf_test_case basic
basic_head() {
	atf_set "descr" "A basic test of unifdef(1) (PR bin/42628)"
	atf_set "require.progs" "unifdef"
}

basic_body() {

	atf_check -s ignore -o file:$(atf_get_srcdir)/d_basic.out \
		-x "unifdef -U__FreeBSD__ $(atf_get_srcdir)/d_basic.in"
}

atf_test_case lastline
lastline_head() {
	atf_set "descr" "Checks with directive on last line (PR bin/47068)"
}

lastline_body() {

	# With newline after cpp directive
	printf '#ifdef foo\n#endif\n' >input
	atf_check -o file:input unifdef -Ubar input

	# Without newline after cpp directive
	printf '#ifdef foo\n#endif' >input
	atf_check -o file:input unifdef -Ubar input
}

atf_init_test_cases() {
	atf_add_test_case basic
	atf_add_test_case lastline
}
