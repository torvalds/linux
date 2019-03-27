#       $NetBSD: t_exec.sh,v 1.9 2016/08/10 21:10:18 kre Exp $
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

rumpsrv='rump_server -lrumpnet -lrumpnet_net -lrumpnet_netinet -lrumpdev -lrumpvfs'
export RUMP_SERVER=unix://csock
export RUMPHIJACK_RETRYCONNECT='die'

atf_test_case noexec cleanup
noexec_head()
{
        atf_set "descr" "check that we see what we expect without exec"
}

noexec_body()
{

	atf_check -s exit:0 ${rumpsrv} ${RUMP_SERVER}
	atf_check -s exit:0 env $(atf_get_srcdir)/h_exec
	atf_check -s exit:0 -o save:sstat.out rump.sockstat -n
	atf_check -s exit:0 -o match:'^root.*h_exec.*tcp.*\*\.1234' \
	    sed -n 2p sstat.out
	atf_check -s exit:0 -o match:'^root.*h_exec.*tcp.*\*\.2345' \
	    sed -n 3p sstat.out
}

noexec_cleanup()
{
	rump.halt
}

atf_test_case exec cleanup
exec_head()
{
        atf_set "descr" "check that client names changes after exec"
}

exec_body()
{

	atf_check -s exit:0 ${rumpsrv} ${RUMP_SERVER}
	atf_check -s exit:0 $(atf_get_srcdir)/h_exec $(atf_get_srcdir)/h_exec
	atf_check -s exit:0 -o save:sstat.out rump.sockstat -n
	atf_check -s exit:0 -o match:'^root.*h_ution.*tcp.*\*\.1234' \
	    sed -n 2p sstat.out
	atf_check -s exit:0 -o match:'^root.*h_ution.*tcp.*\*\.2345' \
	    sed -n 3p sstat.out
}

exec_cleanup()
{
	rump.halt
}

atf_test_case cloexec cleanup
cloexec_head()
{
        atf_set "descr" "check that FD_CLOEXEC works"
}

cloexec_body()
{

	atf_check -s exit:0 ${rumpsrv} ${RUMP_SERVER}
	atf_check -s exit:0  \
	    $(atf_get_srcdir)/h_exec $(atf_get_srcdir)/h_exec cloexec1
	atf_check -s exit:0 -o save:sstat.out rump.sockstat -n
	atf_check -s exit:0 -o inline:'2\n' sed -n '$=' sstat.out
	atf_check -s exit:0 -o match:'^root.*h_ution.*tcp.*\*\.2345' \
	    sed -n 2p sstat.out
}

cloexec_cleanup()
{
	rump.halt
}

atf_test_case vfork cleanup
vfork_head()
{
        atf_set "descr" "test rumpclient_vfork()"
}

vfork_body()
{

	atf_check -s exit:0 ${rumpsrv} ${RUMP_SERVER}
	atf_check -s exit:0  \
	    $(atf_get_srcdir)/h_exec $(atf_get_srcdir)/h_exec vfork_please
	atf_check -s exit:0 -o save:sstat.out rump.sockstat -n
	atf_check -s exit:0 -o inline:'5\n' sed -n '$=' sstat.out
	atf_check -s exit:0 -o match:'^root.*h_ution.*tcp.*\*\.1234' \
	    cat sstat.out
	atf_check -s exit:0 -o match:'^root.*h_ution.*tcp.*\*\.2345' \
	    cat sstat.out
	atf_check -s exit:0 -o match:'^root.*fourchette.*tcp.*\*\.1234' \
	    cat sstat.out
	atf_check -s exit:0 -o match:'^root.*fourchette.*tcp.*\*\.2345' \
	    cat sstat.out
}

vfork_cleanup()
{
	rump.halt
}

atf_test_case threxec cleanup
threxec_head()
{
	atf_set "descr" "check that threads are killed before exec continues"
}

threxec_body()
{
	atf_check -s exit:0 rump_server ${RUMP_SERVER}
	atf_check -s exit:0 $(atf_get_srcdir)/h_execthr
}

threxec_cleanup()
{
	rump.halt
}

atf_init_test_cases()
{
	atf_add_test_case noexec
	atf_add_test_case exec
	atf_add_test_case cloexec
	atf_add_test_case vfork
	atf_add_test_case threxec
}
