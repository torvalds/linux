#	$NetBSD: t_flags.sh,v 1.15 2016/12/21 02:46:08 ozaki-r Exp $
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

SOCK_LOCAL=unix://commsock1
SOCK_PEER=unix://commsock2
SOCK_GW=unix://commsock3
BUS=bus1
BUS2=bus2

DEBUG=${DEBUG:-false}

setup_local()
{

	rump_server_start $SOCK_LOCAL
	rump_server_add_iface $SOCK_LOCAL shmif0 $BUS

	export RUMP_SERVER=$SOCK_LOCAL
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 10.0.0.2/24
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 up
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10

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
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10

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

test_lo()
{

	export RUMP_SERVER=$SOCK_LOCAL

	# Up, Host, local
	check_route_flags 127.0.0.1 UHl
}

test_connected()
{

	export RUMP_SERVER=$SOCK_LOCAL

	# Up, Host, LLINFO, local
	check_route_flags 10.0.0.2 UHl

	# Up, Cloning
	check_route_flags 10.0.0/24 UC
}

test_default_gateway()
{

	export RUMP_SERVER=$SOCK_LOCAL

	atf_check -s exit:0 -o ignore rump.route add default 10.0.0.1
	$DEBUG && rump.netstat -rn -f inet

	# Up, Gateway, Static
	check_route_flags default UGS
}

test_static()
{

	export RUMP_SERVER=$SOCK_LOCAL

	# Static route to host
	atf_check -s exit:0 -o ignore rump.route add 10.0.1.1 10.0.0.1
	$DEBUG && rump.netstat -rn -f inet

	# Up, Gateway, Host, Static
	check_route_flags 10.0.1.1 UGHS

	# Static route to network
	atf_check -s exit:0 -o ignore rump.route add -net 10.0.2.0/24 10.0.0.1
	$DEBUG && rump.netstat -rn -f inet

	# Up, Gateway, Static
	check_route_flags 10.0.2/24 UGS
}

test_blackhole()
{

	export RUMP_SERVER=$SOCK_LOCAL

	atf_check -s exit:0 -o ignore rump.ping -n -w 1 -c 1 10.0.0.1

	# Delete an existing route first
	atf_check -s exit:0 -o ignore rump.route delete -net 10.0.0.0/24

	# Gateway must be lo0
	atf_check -s exit:0 -o ignore \
	    rump.route add -net 10.0.0.0/24 127.0.0.1 -blackhole
	$DEBUG && rump.netstat -rn -f inet

	# Up, Gateway, Blackhole, Static
	check_route_flags 10.0.0/24 UGBS

	atf_check -s not-exit:0 -o match:'100.0% packet loss' \
	    rump.ping -n -w 1 -c 1 10.0.0.1
	$DEBUG && rump.netstat -rn -f inet

	# Shouldn't be created
	check_route_no_entry 10.0.0.1
}

test_reject()
{

	export RUMP_SERVER=$SOCK_LOCAL

	# Delete an existing route first
	atf_check -s exit:0 -o ignore rump.route delete -net 10.0.0.0/24

	atf_check -s exit:0 -o ignore rump.route add -net 10.0.0.0/24 10.0.0.1 -reject
	$DEBUG && rump.netstat -rn -f inet

	# Up, Gateway, Reject, Static
	check_route_flags 10.0.0/24 UGRS

	atf_check -s not-exit:0 -o ignore -e match:'No route to host' \
	    rump.ping -n -w 1 -c 1 10.0.0.1
	$DEBUG && rump.netstat -rn -f inet

	# Shouldn't be created
	check_route_no_entry 10.0.0.1

	# Gateway is lo0 (RTF_GATEWAY)

	# Delete an existing route first
	atf_check -s exit:0 -o ignore rump.route delete -net 10.0.0.0/24

	atf_check -s exit:0 -o ignore \
	    rump.route add -net 10.0.0.0/24 127.0.0.1 -reject
	$DEBUG && rump.netstat -rn -f inet

	# Up, Gateway, Reject, Static
	check_route_flags 10.0.0/24 UGRS

	atf_check -s not-exit:0 -o ignore -e match:'Network is unreachable' \
	    rump.ping -n -w 1 -c 1 10.0.0.1
	$DEBUG && rump.netstat -rn -f inet

	# Shouldn't be created
	check_route_no_entry 10.0.0.1

	# Gateway is lo0 (RTF_HOST)

	# Delete an existing route first
	atf_check -s exit:0 -o ignore rump.route delete -net 10.0.0.0/24

	atf_check -s exit:0 -o ignore \
	    rump.route add -host 10.0.0.1/24 127.0.0.1 -iface -reject
	$DEBUG && rump.netstat -rn -f inet

	# Up, Host, Reject, Static
	check_route_flags 10.0.0.1 UHRS

	atf_check -s not-exit:0 -o ignore -e match:'No route to host' \
	    rump.ping -n -w 1 -c 1 10.0.0.1
	$DEBUG && rump.netstat -rn -f inet

	return 0
}

test_icmp_redirect()
{

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

	export RUMP_SERVER=$SOCK_PEER
	$DEBUG && rump.netstat -rn -f inet

	### Testing Modified flag ###

	#
	# Teach a wrong route to 10.0.2.2
	#
	export RUMP_SERVER=$SOCK_LOCAL
	atf_check -s exit:0 -o ignore rump.route add 10.0.2.2 10.0.0.1
	# Up, Gateway, Host, Static
	check_route_flags 10.0.2.2 UGHS
	check_route_gw 10.0.2.2 10.0.0.1

	# Try ping 10.0.2.2
	atf_check -s exit:0 -o ignore rump.ping -n -w 1 -c 1 10.0.2.2
	$DEBUG && rump.netstat -rn -f inet

	# Up, Gateway, Host, Modified, Static
	check_route_flags 10.0.2.2 UGHMS
	check_route_gw 10.0.2.2 10.0.0.254
}

test_announce()
{
	export RUMP_SERVER=$SOCK_LOCAL

	# Delete an existing route first
	atf_check -s exit:0 -o ignore rump.route delete -net 10.0.0.0/24

	atf_check -s exit:0 -o ignore rump.route add -net 10.0.0.0/24 10.0.0.1 -proxy
	$DEBUG && rump.netstat -rn -f inet

	# Up, Gateway, Static, proxy
	check_route_flags 10.0.0/24 UGSp

	# TODO test its behavior
}

add_test()
{
	local name=$1
	local desc="$2"

	atf_test_case "route_flags_${name}" cleanup
	eval "route_flags_${name}_head() { \
			atf_set \"descr\" \"${desc}\"; \
			atf_set \"require.progs\" \"rump_server\"; \
		}; \
	    route_flags_${name}_body() { \
			setup_local; \
			setup_peer; \
			test_${name}; \
			rump_server_destroy_ifaces; \
		}; \
	    route_flags_${name}_cleanup() { \
			$DEBUG && dump; \
			cleanup; \
		}"
	atf_add_test_case "route_flags_${name}"
}

atf_init_test_cases()
{

	add_test lo              "Tests route flags: loop back interface"
	add_test connected       "Tests route flags: connected route"
	add_test default_gateway "Tests route flags: default gateway"
	add_test static          "Tests route flags: static route"
	add_test blackhole       "Tests route flags: blackhole route"
	add_test reject          "Tests route flags: reject route"
	add_test icmp_redirect   "Tests route flags: icmp redirect"
	add_test announce        "Tests route flags: announce flag"
}
