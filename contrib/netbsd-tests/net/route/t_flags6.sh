#	$NetBSD: t_flags6.sh,v 1.12 2016/12/21 02:46:08 ozaki-r Exp $
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

SOCK_LOCAL=unix://commsock1
SOCK_PEER=unix://commsock2
SOCK_GW=unix://commsock3
BUS=bus1
BUS2=bus2

IP6_LOCAL=fc00::2
IP6_PEER=fc00::1

DEBUG=${DEBUG:-false}

setup_local()
{

	rump_server_start $SOCK_LOCAL netinet6
	rump_server_add_iface $SOCK_LOCAL shmif0 $BUS

	export RUMP_SERVER=$SOCK_LOCAL
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 inet6 $IP6_LOCAL
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 up
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10

	$DEBUG && rump.ifconfig
	$DEBUG && rump.netstat -rn -f inet6
}

setup_peer()
{

	rump_server_start $SOCK_PEER netinet6
	rump_server_add_iface $SOCK_PEER shmif0 $BUS

	export RUMP_SERVER=$SOCK_PEER
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 inet6 $IP6_PEER
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 up
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10

	$DEBUG && rump.ifconfig
	$DEBUG && rump.netstat -rn -f inet6
}

test_lo6()
{

	export RUMP_SERVER=$SOCK_LOCAL

	# Up, Host, local
	check_route_flags fe80::1 UHl

	# Up, Host, local
	check_route_flags ::1 UHl
}

test_connected6()
{

	export RUMP_SERVER=$SOCK_LOCAL

	# Up, Host, local
	check_route_flags $IP6_LOCAL UHl

	# Up, Connected
	check_route_flags fc00::/64 UC
}

test_default_gateway6()
{

	export RUMP_SERVER=$SOCK_LOCAL

	atf_check -s exit:0 -o ignore rump.route add -inet6 default $IP6_PEER
	$DEBUG && rump.netstat -rn -f inet6

	# Up, Gateway, Static
	check_route_flags default UGS
}

test_static6()
{

	export RUMP_SERVER=$SOCK_LOCAL

	# Static route to host
	atf_check -s exit:0 -o ignore \
	    rump.route add -inet6 fc00::1:1 $IP6_PEER
	$DEBUG && rump.netstat -rn -f inet6

	# Up, Gateway, Host, Static
	check_route_flags fc00::1:1 UGHS

	# Static route to network
	atf_check -s exit:0 -o ignore \
	    rump.route add -inet6 -net fc00::/24 $IP6_PEER
	$DEBUG && rump.netstat -rn -f inet6

	# Up, Gateway, Static
	check_route_flags fc00::/24 UGS
}

test_blackhole6()
{

	export RUMP_SERVER=$SOCK_LOCAL

	atf_check -s exit:0 -o ignore rump.ping6 -n -X 1 -c 1 $IP6_PEER

	# Delete an existing route first
	atf_check -s exit:0 -o ignore \
	    rump.route delete -inet6 -net fc00::/64

	# Gateway must be lo0
	atf_check -s exit:0 -o ignore \
	    rump.route add -inet6 -net fc00::/64 ::1 -blackhole
	$DEBUG && rump.netstat -rn -f inet6

	# Up, Gateway, Blackhole, Static
	check_route_flags fc00::/64 UGBS

	atf_check -s not-exit:0 -o match:'100.0% packet loss' \
	    rump.ping6 -n -X 1 -c 1 $IP6_PEER
	$DEBUG && rump.netstat -rn -f inet6

	# Shouldn't be created
	check_route_no_entry $IP6_PEER
}

test_reject6()
{

	export RUMP_SERVER=$SOCK_LOCAL

	# Delete an existing route first
	atf_check -s exit:0 -o ignore \
	    rump.route delete -inet6 -net fc00::/64

	atf_check -s exit:0 -o ignore \
	    rump.route add -inet6 -net fc00::/64 $IP6_PEER -reject
	$DEBUG && rump.netstat -rn -f inet6

	# Up, Gateway, Reject, Static
	check_route_flags fc00::/64 UGRS

	atf_check -s not-exit:0 -o ignore -e match:'No route to host' \
	    rump.ping6 -n -X 1 -c 1 $IP6_PEER
	$DEBUG && rump.netstat -rn -f inet6

	# Shouldn't be created
	check_route_no_entry $IP6_PEER

	# Gateway is lo0 (RTF_GATEWAY)

	# Delete an existing route first
	atf_check -s exit:0 -o ignore \
	    rump.route delete -inet6 -net fc00::/64

	atf_check -s exit:0 -o ignore \
	    rump.route add -inet6 -net fc00::/64 ::1  -reject
	$DEBUG && rump.netstat -rn -f inet6

	# Up, Gateway, Reject, Static
	check_route_flags fc00::/64 UGRS

	atf_check -s not-exit:0 -o ignore -e match:'Network is unreachable' \
	    rump.ping6 -n -X 1 -c 1 $IP6_PEER
	$DEBUG && rump.netstat -rn -f inet6

	# Shouldn't be created
	check_route_no_entry $IP6_PEER

	# Gateway is lo0 (RTF_HOST)

	# Delete an existing route first
	atf_check -s exit:0 -o ignore \
	    rump.route delete -inet6 -net fc00::/64

	atf_check -s exit:0 -o ignore \
	    rump.route add -inet6 -host fc00::/64 ::1 -iface -reject
	$DEBUG && rump.netstat -rn -f inet6

	# Up, Host, Reject, Static
	check_route_flags fc00:: UHRS

	atf_check -s not-exit:0 -o ignore -e match:'No route to host' \
	    rump.ping6 -n -X 1 -c 1 $IP6_PEER
	$DEBUG && rump.netstat -rn -f inet6

	return 0
}

test_announce6()
{
	export RUMP_SERVER=$SOCK_LOCAL

	# Delete an existing route first
	atf_check -s exit:0 -o ignore \
	    rump.route delete -inet6 -net fc00::/64

	atf_check -s exit:0 -o ignore \
	    rump.route add -inet6 -net fc00::/64 $IP6_PEER -proxy
	$DEBUG && rump.netstat -rn -f inet6

	# Up, Gateway, Static, proxy
	check_route_flags fc00::/64 UGSp

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

	add_test lo6              "Tests route flags: loop back interface"
	add_test connected6       "Tests route flags: connected route"
	add_test default_gateway6 "Tests route flags: default gateway"
	add_test static6          "Tests route flags: static route"
	add_test blackhole6       "Tests route flags: blackhole route"
	add_test reject6          "Tests route flags: reject route"
	add_test announce6        "Tests route flags: announce flag"
}
