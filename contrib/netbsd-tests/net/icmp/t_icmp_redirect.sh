#	$NetBSD: t_icmp_redirect.sh,v 1.6 2016/11/25 08:51:16 ozaki-r Exp $
#
# Copyright (c) 2015 The NetBSD Foundation, Inc.
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

# Most codes are derived from tests/net/route/t_flags.sh

SOCK_LOCAL=unix://commsock1
SOCK_PEER=unix://commsock2
SOCK_GW=unix://commsock3
BUS=bus1
BUS2=bus2
REDIRECT_TIMEOUT=5

DEBUG=${DEBUG:-false}

atf_test_case icmp_redirect_timeout cleanup

icmp_redirect_timeout_head()
{

	atf_set "descr" "Tests for ICMP redirect timeout";
	atf_set "require.progs" "rump_server";
}

setup_local()
{

	rump_server_start $SOCK_LOCAL
	rump_server_add_iface $SOCK_LOCAL shmif0 $BUS

	export RUMP_SERVER=$SOCK_LOCAL
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 10.0.0.2/24
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 up

	atf_check -s exit:0 -o ignore rump.sysctl -w \
	    net.inet.icmp.redirtimeout=$REDIRECT_TIMEOUT

	$DEBUG && rump.ifconfig
	$DEBUG && rump.netstat -rn -f inet
}

setup_peer()
{

	rump_server_start $SOCK_PEER
	rump_server_add_iface $SOCK_PEER shmif0 $BUS

	export RUMP_SERVER=$SOCK_PEER
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 10.0.0.1/24
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 up

	$DEBUG && rump.ifconfig
	$DEBUG && rump.netstat -rn -f inet
}

setup_gw()
{

	rump_server_start $SOCK_GW
	rump_server_add_iface $SOCK_GW shmif0 $BUS
	rump_server_add_iface $SOCK_GW shmif1 $BUS2

	export RUMP_SERVER=$SOCK_GW
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 10.0.0.254/24
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 up

	atf_check -s exit:0 -o ignore rump.ifconfig shmif1 10.0.2.1/24
	atf_check -s exit:0 -o ignore rump.ifconfig shmif1 alias 10.0.2.2/24
	atf_check -s exit:0 -o ignore rump.ifconfig shmif1 up

	# Wait until DAD completes (10 sec at most)
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10
	atf_check -s not-exit:0 -x "rump.ifconfig shmif1 |grep -q tentative"

	$DEBUG && rump.ifconfig
	$DEBUG && rump.netstat -rn -f inet
}

icmp_redirect_timeout_body()
{

	$DEBUG && ulimit -c unlimited

	setup_local
	setup_peer

	### Testing Dynamic flag ###

	#
	# Setup a gateway 10.0.0.254. 10.0.2.1 is behind it.
	#
	setup_gw

	#
	# Teach the peer that 10.0.2.* is behind 10.0.0.254
	#
	export RUMP_SERVER=$SOCK_PEER
	atf_check -s exit:0 -o ignore rump.route add -net 10.0.2.0/24 10.0.0.254
	# Up, Gateway, Static
	check_route_flags 10.0.2/24 UGS

	#
	# Setup the default gateway to the peer, 10.0.0.1
	#
	export RUMP_SERVER=$SOCK_LOCAL
	atf_check -s exit:0 -o ignore rump.route add default 10.0.0.1
	# Up, Gateway, Static
	check_route_flags default UGS

	# Try ping 10.0.2.1
	atf_check -s exit:0 -o ignore rump.ping -n -w 1 -c 1 10.0.2.1
	$DEBUG && rump.netstat -rn -f inet

	# Up, Gateway, Host, Dynamic
	check_route_flags 10.0.2.1 UGHD
	check_route_gw 10.0.2.1 10.0.0.254

	atf_check -s exit:0 sleep $((REDIRECT_TIMEOUT + 2))

	# The dynamic entry should be expired and removed
	check_route_no_entry 10.0.2.1

	export RUMP_SERVER=$SOCK_PEER
	$DEBUG && rump.netstat -rn -f inet

	rump_server_destroy_ifaces
}

icmp_redirect_timeout_cleanup()
{

	$DEBUG && dump
	cleanup
}

atf_test_case icmp_redirect cleanup

icmp_redirect_head()
{

	atf_set "descr" "Tests for icmp redirect";
	atf_set "require.progs" "rump_server";
}

setup_redirect()
{
	atf_check -s exit:0 -o ignore rump.sysctl -w \
	    net.inet.ip.redirect=1
}

teardown_redirect()
{
	atf_check -s exit:0 -o ignore rump.sysctl -w \
	    net.inet.ip.redirect=0
}

icmp_redirect_body()
{

	$DEBUG && ulimit -c unlimited

	setup_local
	setup_peer

	#
	# Setup a gateway 10.0.0.254. 10.0.2.1 is behind it.
	#
	setup_gw

	#
	# Teach the peer that 10.0.2.* is behind 10.0.0.254
	#
	export RUMP_SERVER=$SOCK_PEER
	atf_check -s exit:0 -o ignore rump.route add -net 10.0.2.0/24 10.0.0.254
	# Up, Gateway, Static
	check_route_flags 10.0.2/24 UGS

	#
	# Setup the default gateway to the peer, 10.0.0.1
	#
	export RUMP_SERVER=$SOCK_LOCAL
	atf_check -s exit:0 -o ignore rump.route add default 10.0.0.1
	# Up, Gateway, Static
	check_route_flags default UGS


	### ICMP redirects are NOT sent by the peer ###

	#
	# Disable net.inet.ip.redirect
	#
	export RUMP_SERVER=$SOCK_PEER
	teardown_redirect

	# Try ping 10.0.2.1
	export RUMP_SERVER=$SOCK_LOCAL
	atf_check -s exit:0 -o ignore rump.ping -n -w 1 -c 1 10.0.2.1
	$DEBUG && rump.netstat -rn -f inet

	# A direct route shouldn't be created
	check_route_no_entry 10.0.2.1


	### ICMP redirects are sent by the peer ###

	#
	# Enable net.inet.ip.redirect
	#
	export RUMP_SERVER=$SOCK_PEER
	setup_redirect

	# Try ping 10.0.2.1
	export RUMP_SERVER=$SOCK_LOCAL
	atf_check -s exit:0 -o ignore rump.ping -n -w 1 -c 1 10.0.2.1
	$DEBUG && rump.netstat -rn -f inet

	# Up, Gateway, Host, Dynamic
	check_route_flags 10.0.2.1 UGHD
	check_route_gw 10.0.2.1 10.0.0.254

	export RUMP_SERVER=$SOCK_PEER
	$DEBUG && rump.netstat -rn -f inet


	# cleanup
	export RUMP_SERVER=$SOCK_LOCAL
	atf_check -s exit:0 -o ignore rump.route delete 10.0.2.1
	check_route_no_entry 10.0.2.1


	### ICMP redirects are NOT sent by the peer (again) ###

	#
	# Disable net.inet.ip.redirect
	#
	export RUMP_SERVER=$SOCK_PEER
	teardown_redirect

	# Try ping 10.0.2.1
	export RUMP_SERVER=$SOCK_LOCAL
	atf_check -s exit:0 -o ignore rump.ping -n -w 1 -c 1 10.0.2.1
	$DEBUG && rump.netstat -rn -f inet

	# A direct route shouldn't be created
	check_route_no_entry 10.0.2.1

	rump_server_destroy_ifaces
}

icmp_redirect_cleanup()
{

	$DEBUG && dump
	cleanup
}

atf_init_test_cases()
{

	atf_add_test_case icmp_redirect
	atf_add_test_case icmp_redirect_timeout
}
