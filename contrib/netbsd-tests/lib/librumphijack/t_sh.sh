#       $NetBSD: t_sh.sh,v 1.1 2011/03/03 11:54:12 pooka Exp $
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

#
# Test various /bin/sh descriptor games.
#
# Note that there is an extra level of trickery here, since
# we need to run an extra level of shell to "catch" LD_PRELOAD.
#

rumpsrv='rump_server -lrumpvfs'
export RUMP_SERVER=unix://csock
exout="this is the output you are looking for"

atf_test_case runscript cleanup
runscript_head()
{
        atf_set "descr" "can run /bin/sh scripts from rumpfs"
}

runscript_body()
{
	atf_check -s exit:0 ${rumpsrv} ${RUMP_SERVER}

	export LD_PRELOAD=/usr/lib/librumphijack.so
	echo "echo $exout" > thescript
	atf_check -s exit:0 mv thescript /rump
	atf_check -s exit:0 -o inline:"${exout}\n" -x sh /rump/thescript
}

runscript_cleanup()
{
	rump.halt
}

atf_test_case redirect cleanup
redirect_head()
{
        atf_set "descr" "input/output redirection works with rumphijack"
}

redirect_body()
{
	atf_check -s exit:0 ${rumpsrv} ${RUMP_SERVER}
	export LD_PRELOAD=/usr/lib/librumphijack.so

	echo "echo $exout > /rump/thefile" > thescript
	atf_check -s exit:0 -x sh thescript

	# read it without input redirection
	atf_check -s exit:0 -o inline:"${exout}\n" cat /rump/thefile

	# read it with input redirection (note, need an exec'd shell again)
	echo "cat < /rump/thefile" > thescript
	atf_check -s exit:0 -o inline:"${exout}\n" -x sh thescript
}

redirect_cleanup()
{
	rump.halt
}

atf_init_test_cases()
{
	atf_add_test_case runscript
	atf_add_test_case redirect
}
