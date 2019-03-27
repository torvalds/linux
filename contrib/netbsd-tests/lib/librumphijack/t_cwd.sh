#       $NetBSD: t_cwd.sh,v 1.2 2011/02/19 19:57:28 pooka Exp $
#
# Copyright (c) 2011 The NetBSD Foundation, Inc.
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

rumpsrv='rump_server -lrumpvfs'
export RUMP_SERVER=unix://csock

test_case()
{
	local name="${1}"; shift

	atf_test_case "${name}" cleanup
	eval "${name}_head() {  }"
	eval "${name}_body() { \
		export RUMPHIJACK="path=${1}" ; \
		atf_check -s exit:0 ${rumpsrv} ${RUMP_SERVER} ; \
		testbody " "${@}" "; \
	}"
	eval "${name}_cleanup() { \
		rump.halt
	}"
}

test_case basic_chdir /rump simple chdir
test_case basic_fchdir /rump simple fchdir
test_case slash_chdir // simple chdir
test_case slash_fchdir // simple fchdir
test_case symlink_chdir /rump symlink chdir
test_case symlink_fchdir /rump symlink fchdir
test_case symlink_slash_chdir // symlink chdir
test_case symlink_slash_fchdir // symlink fchdir

testbody()
{
	atf_check -s exit:0 env LD_PRELOAD=/usr/lib/librumphijack.so \
	    $(atf_get_srcdir)/h_cwd $*
}

atf_init_test_cases()
{
	atf_add_test_case basic_chdir
	atf_add_test_case basic_fchdir
	atf_add_test_case slash_chdir
	atf_add_test_case slash_fchdir
	atf_add_test_case symlink_chdir
	atf_add_test_case symlink_fchdir
	atf_add_test_case symlink_slash_chdir
	atf_add_test_case symlink_slash_fchdir
}
