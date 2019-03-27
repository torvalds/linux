# $NetBSD: t_find.sh,v 1.6 2012/03/19 12:58:41 jruoho Exp $
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

atf_test_case emptyperm
emptyperm_head() {
	atf_set "descr" "Test that 'find -empty' does not error out " \
			"when directory access is denied (PR bin/44179)"
	atf_set "require.user" "unprivileged"
}

emptyperm_body() {

	# The case assumes that at least some directories
	# in /var are unavailable for the user '_tests'.
	#
	# TODO: Parse the output.file for actual verification.
	#
	atf_check -s exit:1 -o save:output.file \
		-e not-empty -x "find /var -empty -type d"
}

atf_test_case exit
exit_head() {
	atf_set "descr" "Test that find(1) with -exit works (PR bin/44973)"
}

exit_body() {
	atf_check -o ignore \
		-s exit:0 -x "find /etc -type f -exit"
}

atf_test_case exit_status
exit_status_head() {
	atf_set "descr" "Test exit status from 'find -exit'"
}

exit_status_body() {
	num=$(jot -r 1 0 99)
	atf_check -o ignore -e ignore -s exit:$num -x "find / -exit $num"
}

atf_init_test_cases() {
	atf_add_test_case emptyperm
	atf_add_test_case exit
	atf_add_test_case exit_status
}
