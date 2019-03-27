#	$NetBSD: t_pppoe.sh,v 1.16 2016/12/14 03:30:30 knakahara Exp $
#
# Copyright (c) 2016 Internet Initiative Japan Inc.
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

server="rump_server -lrump -lrumpnet -lrumpnet_net -lrumpnet_netinet	\
		    -lrumpnet_netinet6 -lrumpnet_shmif -lrumpdev	\
		    -lrumpnet_pppoe"
# pppoectl doesn't work with RUMPHIJACK=sysctl=yes
HIJACKING="env LD_PRELOAD=/usr/lib/librumphijack.so"

SERVER=unix://commsock1
CLIENT=unix://commsock2

SERVER_IP=10.3.3.1
CLIENT_IP=10.3.3.3
SERVER_IP6=fc00::1
CLIENT_IP6=fc00::3
AUTHNAME=foobar@baz.com
SECRET=oink
BUS=bus0
TIMEOUT=3
WAITTIME=10
DEBUG=${DEBUG:-false}

setup()
{
	inet=true

	if [ $# -ne 0 ]; then
		eval $@
	fi

	atf_check -s exit:0 ${server} $SERVER
	atf_check -s exit:0 ${server} $CLIENT

	export RUMP_SERVER=$SERVER
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr $BUS
	atf_check -s exit:0 rump.ifconfig shmif0 up

	atf_check -s exit:0 rump.ifconfig pppoe0 create
	$inet && atf_check -s exit:0 rump.ifconfig pppoe0 \
	    inet $SERVER_IP $CLIENT_IP down
	atf_check -s exit:0 rump.ifconfig pppoe0 link0

	$DEBUG && rump.ifconfig
	$DEBUG && $HIJACKING pppoectl -d pppoe0

	atf_check -s exit:0 -x "$HIJACKING pppoectl -e shmif0 pppoe0"
	unset RUMP_SERVER

	export RUMP_SERVER=$CLIENT
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr $BUS
	atf_check -s exit:0 rump.ifconfig shmif0 up

	atf_check -s exit:0 rump.ifconfig pppoe0 create
	$inet && atf_check -s exit:0 rump.ifconfig pppoe0 \
	    inet 0.0.0.0 0.0.0.1 down

	atf_check -s exit:0 -x "$HIJACKING pppoectl -e shmif0 pppoe0"
	unset RUMP_SERVER
}

cleanup()
{
	env RUMP_SERVER=$SERVER rump.halt
	env RUMP_SERVER=$CLIENT rump.halt
}


wait_for_session_established()
{
	local dontfail=$1
	local n=$WAITTIME

	for i in $(seq $n); do
		$HIJACKING pppoectl -d pppoe0 |grep -q "state = session"
		[ $? = 0 ] && return
		sleep 1
	done

	if [ "$dontfail" != "dontfail" ]; then
		atf_fail "Couldn't connect to the server for $n seconds."
	fi
}

wait_for_disconnected()
{
	local dontfail=$1
	local n=$WAITTIME

	for i in $(seq $n); do
		$HIJACKING pppoectl -d pppoe0 | grep -q "state = initial"
		[ $? = 0 ] && return
		# If PPPoE client is disconnected by PPPoE server and then
		# the client kicks callout of pppoe_timeout(), the client
		# state is changed to PPPOE_STATE_PADI_SENT while padi retrying.
		$HIJACKING pppoectl -d pppoe0 | grep -q "state = PADI sent"
		[ $? = 0 ] && return

		sleep 1
	done

	if [ "$dontfail" != "dontfail" ]; then
		atf_fail "Couldn't disconnect for $n seconds."
	fi
}

run_test()
{
	local auth=$1
	setup

	# As pppoe client doesn't support rechallenge yet.
	local server_optparam=""
	if [ $auth = "chap" ]; then
		server_optparam="norechallenge"
	fi

	export RUMP_SERVER=$SERVER
	local setup_serverparam="pppoectl pppoe0 hisauthproto=$auth \
				    'hisauthname=$AUTHNAME' \
				    'hisauthsecret=$SECRET' \
				    'myauthproto=none' \
				    $server_optparam"
	atf_check -s exit:0 -x "$HIJACKING $setup_serverparam"
	atf_check -s exit:0 rump.ifconfig pppoe0 up
	unset RUMP_SERVER

	export RUMP_SERVER=$CLIENT
	local setup_clientparam="pppoectl pppoe0 myauthproto=$auth \
				    'myauthname=$AUTHNAME' \
				    'myauthsecret=$SECRET' \
				    'hisauthproto=none'"
	atf_check -s exit:0 -x "$HIJACKING $setup_clientparam"
	atf_check -s exit:0 rump.ifconfig pppoe0 up
	$DEBUG && rump.ifconfig
	wait_for_session_established
	atf_check -s exit:0 -o ignore rump.ping -c 1 -w $TIMEOUT $SERVER_IP
	unset RUMP_SERVER

	# test for disconnection from server
	export RUMP_SERVER=$SERVER
	atf_check -s exit:0 rump.ifconfig pppoe0 down
	wait_for_disconnected
	export RUMP_SERVER=$CLIENT
	wait_for_disconnected
	atf_check -s not-exit:0 -o ignore -e ignore \
	    rump.ping -c 1 -w $TIMEOUT $SERVER_IP
	atf_check -s exit:0 -o match:'PADI sent' -x "$HIJACKING pppoectl -d pppoe0"
	unset RUMP_SERVER

	# test for recoonecting
	atf_check -s exit:0 -x "env RUMP_SERVER=$SERVER rump.ifconfig pppoe0 up"
	export RUMP_SERVER=$CLIENT
	wait_for_session_established
	atf_check -s exit:0 -o ignore rump.ping -c 1 -w $TIMEOUT $SERVER_IP
	unset RUMP_SERVER

	# test for disconnection from client
	export RUMP_SERVER=$CLIENT
	atf_check -s exit:0 -x rump.ifconfig pppoe0 down
	wait_for_disconnected
	export RUMP_SERVER=$SERVER
	wait_for_disconnected
	$DEBUG && $HIJACKING pppoectl -d pppoe0
	atf_check -s not-exit:0 -o ignore -e ignore \
	    rump.ping -c 1 -w $TIMEOUT $CLIENT_IP
	atf_check -s exit:0 -o match:'initial' -x "$HIJACKING pppoectl -d pppoe0"
	unset RUMP_SERVER

	# test for reconnecting
	export RUMP_SERVER=$CLIENT
	atf_check -s exit:0 -x rump.ifconfig pppoe0 up
	wait_for_session_established
	$DEBUG && rump.ifconfig pppoe0
	$DEBUG && $HIJACKING pppoectl -d pppoe0
	unset RUMP_SERVER

	export RUMP_SERVER=$SERVER
	atf_check -s exit:0 rump.ifconfig -w 10
	atf_check -s exit:0 -o ignore rump.ping -c 1 -w $TIMEOUT $CLIENT_IP
	atf_check -s exit:0 -o match:'session' -x "$HIJACKING pppoectl -d pppoe0"
	$DEBUG && HIJACKING pppoectl -d pppoe0
	unset RUMP_SERVER

	# test for invalid password
	export RUMP_SERVER=$CLIENT
	atf_check -s exit:0 rump.ifconfig pppoe0 down
	wait_for_disconnected
	local setup_clientparam="pppoectl pppoe0 myauthproto=$auth \
				    'myauthname=$AUTHNAME' \
				    'myauthsecret=invalidsecret' \
				    'hisauthproto=none'"
	atf_check -s exit:0 -x "$HIJACKING $setup_clientparam"
	atf_check -s exit:0 rump.ifconfig pppoe0 up
	wait_for_session_established dontfail
	atf_check -s not-exit:0 -o ignore -e ignore \
	    rump.ping -c 1 -w $TIMEOUT $SERVER_IP
	atf_check -s exit:0 -o match:'DETACHED' rump.ifconfig pppoe0
	unset RUMP_SERVER
}

atf_test_case pppoe_pap cleanup

pppoe_pap_head()
{
	atf_set "descr" "Does simple pap tests"
	atf_set "require.progs" "rump_server pppoectl"
}

pppoe_pap_body()
{
	run_test pap
}

pppoe_pap_cleanup()
{
	cleanup
}

atf_test_case pppoe_chap cleanup

pppoe_chap_head()
{
	atf_set "descr" "Does simple chap tests"
	atf_set "require.progs" "rump_server pppoectl"
}

pppoe_chap_body()
{
	run_test chap
}

pppoe_chap_cleanup()
{
	cleanup
}

run_test6()
{
	local auth=$1
	setup "inet=false"

	# As pppoe client doesn't support rechallenge yet.
	local server_optparam=""
	if [ $auth = "chap" ]; then
		server_optparam="norechallenge"
	fi

	export RUMP_SERVER=$SERVER
	local setup_serverparam="pppoectl pppoe0 hisauthproto=$auth \
				    'hisauthname=$AUTHNAME' \
				    'hisauthsecret=$SECRET' \
				    'myauthproto=none' \
				    $server_optparam"
	atf_check -s exit:0 -x "$HIJACKING $setup_serverparam"
	atf_check -s exit:0 rump.ifconfig pppoe0 inet6 $SERVER_IP6/64 down
	atf_check -s exit:0 rump.ifconfig pppoe0 up
	unset RUMP_SERVER

	export RUMP_SERVER=$CLIENT
	local setup_clientparam="pppoectl pppoe0 myauthproto=$auth \
				    'myauthname=$AUTHNAME' \
				    'myauthsecret=$SECRET' \
				    'hisauthproto=none'"
	atf_check -s exit:0 -x "$HIJACKING $setup_clientparam"
	atf_check -s exit:0 rump.ifconfig pppoe0 inet6 $CLIENT_IP6/64 down
	atf_check -s exit:0 rump.ifconfig pppoe0 up
	$DEBUG && rump.ifconfig
	wait_for_session_established
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10
	export RUMP_SERVER=$SERVER
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10
	export RUMP_SERVER=$CLIENT
	atf_check -s exit:0 -o ignore rump.ping6 -c 1 -X $TIMEOUT $SERVER_IP6
	unset RUMP_SERVER

	# test for disconnection from server
	export RUMP_SERVER=$SERVER
	session_id=`$HIJACKING pppoectl -d pppoe0 | grep state`
	atf_check -s exit:0 rump.ifconfig pppoe0 down
	wait_for_disconnected
	export RUMP_SERVER=$CLIENT
	wait_for_disconnected
	atf_check -s not-exit:0 -o ignore -e ignore \
	    rump.ping6 -c 1 -X $TIMEOUT $SERVER_IP6
	atf_check -s exit:0 -o not-match:"$session_id" -x "$HIJACKING pppoectl -d pppoe0"
	unset RUMP_SERVER

	# test for recoonecting
	export RUMP_SERVER=$SERVER
	atf_check -s exit:0 rump.ifconfig pppoe0 up
	wait_for_session_established
	atf_check -s exit:0 rump.ifconfig -w 10
	$DEBUG && $HIJACKING pppoectl -d pppoe0
	$DEBUG && rump.ifconfig pppoe0
	export RUMP_SERVER=$CLIENT
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10
	atf_check -s exit:0 -o ignore rump.ping6 -c 1 -X $TIMEOUT $SERVER_IP6
	unset RUMP_SERVER

	# test for disconnection from client
	export RUMP_SERVER=$CLIENT
	atf_check -s exit:0 rump.ifconfig pppoe0 down
	wait_for_disconnected

	export RUMP_SERVER=$SERVER
	wait_for_disconnected
	$DEBUG && $HIJACKING pppoectl -d pppoe0
	atf_check -s not-exit:0 -o ignore -e ignore \
	    rump.ping6 -c 1 -X $TIMEOUT $CLIENT_IP6
	atf_check -s exit:0 -o match:'initial' -x "$HIJACKING pppoectl -d pppoe0"
	unset RUMP_SERVER

	# test for reconnecting
	export RUMP_SERVER=$CLIENT
	atf_check -s exit:0 rump.ifconfig pppoe0 up
	wait_for_session_established
	atf_check -s exit:0 rump.ifconfig -w 10

	$DEBUG && rump.ifconfig pppoe0
	$DEBUG && $HIJACKING pppoectl -d pppoe0
	unset RUMP_SERVER

	export RUMP_SERVER=$SERVER
	atf_check -s exit:0 rump.ifconfig -w 10
	atf_check -s exit:0 -o ignore rump.ping6 -c 1 -X $TIMEOUT $CLIENT_IP6
	atf_check -s exit:0 -o match:'session' -x "$HIJACKING pppoectl -d pppoe0"
	$DEBUG && HIJACKING pppoectl -d pppoe0
	unset RUMP_SERVER

	# test for invalid password
	export RUMP_SERVER=$CLIENT
	atf_check -s exit:0 rump.ifconfig pppoe0 down
	wait_for_disconnected
	local setup_clientparam="pppoectl pppoe0 myauthproto=$auth \
				    'myauthname=$AUTHNAME' \
				    'myauthsecret=invalidsecret' \
				    'hisauthproto=none'"
	atf_check -s exit:0 -x "$HIJACKING $setup_clientparam"
	atf_check -s exit:0 rump.ifconfig pppoe0 up
	wait_for_session_established dontfail
	atf_check -s not-exit:0 -o ignore -e ignore \
	    rump.ping6 -c 1 -X $TIMEOUT $SERVER_IP6
	atf_check -s exit:0 -o match:'DETACHED' rump.ifconfig pppoe0
	unset RUMP_SERVER
}

atf_test_case pppoe6_pap cleanup

pppoe6_pap_head()
{
	atf_set "descr" "Does simple pap using IPv6 tests"
	atf_set "require.progs" "rump_server pppoectl"
}

pppoe6_pap_body()
{
	run_test6 pap
}

pppoe6_pap_cleanup()
{
	cleanup
}

atf_test_case pppoe6_chap cleanup

pppoe6_chap_head()
{
	atf_set "descr" "Does simple chap using IPv6 tests"
	atf_set "require.progs" "rump_server pppoectl"
}

pppoe6_chap_body()
{
	run_test6 chap
}

pppoe6_chap_cleanup()
{
	cleanup
}

atf_init_test_cases()
{
	atf_add_test_case pppoe_pap
	atf_add_test_case pppoe_chap
	atf_add_test_case pppoe6_pap
	atf_add_test_case pppoe6_chap
}
