#       $NetBSD: t_asyncio.sh,v 1.4 2014/08/27 13:32:16 gson Exp $
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

rumpsrv='rump_server'
export RUMP_SERVER=unix://csock

atf_test_case select_timeout cleanup
select_timeout_head()
{
        atf_set "descr" "select() with timeout returns no fds set"
}

select_timeout_body()
{

	atf_check -s exit:0 ${rumpsrv} ${RUMP_SERVER}
	atf_check -s exit:0 env LD_PRELOAD=/usr/lib/librumphijack.so \
	    $(atf_get_srcdir)/h_client select_timeout
}

select_timeout_cleanup()
{
	rump.halt
}

atf_test_case select_allunset cleanup
select_allunset_head()
{
        atf_set "descr" "select() with no set fds in fd_set should not crash"
}

select_allunset_body()
{

	atf_check -s exit:0 ${rumpsrv} ${RUMP_SERVER}
	atf_check -s exit:0 env LD_PRELOAD=/usr/lib/librumphijack.so \
	    $(atf_get_srcdir)/h_client select_allunset
}

select_allunset_cleanup()
{
	rump.halt
}

atf_test_case invafd cleanup
invafd_head()
{
        atf_set "descr" "poll on invalid rump fd"
	atf_set "timeout" "10"
}

invafd_body()
{

	atf_check -s exit:0 rump_server -lrumpvfs ${RUMP_SERVER}
	atf_check -s exit:0 env LD_PRELOAD=/usr/lib/librumphijack.so \
	    $(atf_get_srcdir)/h_client invafd
}

invafd_cleanup()
{
	rump.halt
}

atf_init_test_cases()
{
	atf_add_test_case select_timeout
	atf_add_test_case select_allunset
	atf_add_test_case invafd
}
