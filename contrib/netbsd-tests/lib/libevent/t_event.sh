# $NetBSD: t_event.sh,v 1.3 2010/11/29 18:21:15 pgoyette Exp $
#
# Copyright (c) 2009 The NetBSD Foundation, Inc.
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
# This is not great but rather than reimplementing the libevent
# provided regression tests, we use an ATF wrapper around the test
# program which carries out all the tests and prints an extensive
# report.
#

atf_test_case kqueue
kqueue_head() {
	atf_set "descr" "Test libevent with kqueue backend"
}
kqueue_body() {
	EVENT_NOPOLL=1 EVENT_NOSELECT=1 \
	    $(atf_get_srcdir)/h_event 2>&1 || atf_fail "check report"
}

atf_test_case poll
poll_head() {
	atf_set "descr" "Test libevent with poll backend"
}
poll_body() {
	EVENT_NOKQUEUE=1 EVENT_NOSELECT=1 \
	    $(atf_get_srcdir)/h_event 2>&1 || atf_fail "check report"
}

atf_test_case select
select_head() {
	atf_set "descr" "Test libevent with select backend"
}
select_body() {
	EVENT_NOKQUEUE=1 EVENT_NOPOLL=1 \
	    $(atf_get_srcdir)/h_event 2>&1 || atf_fail "check report"
}

atf_init_test_cases()
{
	atf_add_test_case kqueue
	atf_add_test_case poll
	atf_add_test_case select
}
