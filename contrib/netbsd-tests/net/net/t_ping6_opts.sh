#	$NetBSD: t_ping6_opts.sh,v 1.8 2016/11/25 08:51:17 ozaki-r Exp $
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

SOCKSRC=unix://commsock1
SOCKFWD=unix://commsock2
SOCKDST=unix://commsock3
IP6SRC=fc00:0:0:1::2
IP6SRCGW=fc00:0:0:1::1
IP6DSTGW=fc00:0:0:2::1
IP6DST=fc00:0:0:2::2
BUS_SRCGW=bus1
BUS_DSTGW=bus2

IP6SRC2=fc00:0:0:1::3
IP6SRCGW2=fc00:0:0:1::254

DEBUG=${DEBUG:-false}
TIMEOUT=1

#
# Utility functions
#
setup_endpoint()
{
	local sock=${1}
	local addr=${2}
	local bus=${3}
	local gw=${4}

	rump_server_add_iface $sock shmif0 $bus

	export RUMP_SERVER=${sock}
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 ${addr}
	atf_check -s exit:0 -o ignore rump.route add -inet6 default ${gw}
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig -w 10

	if $DEBUG; then
		rump.ifconfig shmif0
		rump.netstat -nr
	fi
}

setup_forwarder()
{

	rump_server_add_iface $SOCKFWD shmif0 $BUS_SRCGW
	rump_server_add_iface $SOCKFWD shmif1 $BUS_DSTGW

	export RUMP_SERVER=$SOCKFWD

	atf_check -s exit:0 rump.ifconfig shmif0 inet6 ${IP6SRCGW}
	atf_check -s exit:0 rump.ifconfig shmif1 inet6 ${IP6DSTGW}

	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig shmif1 up
	atf_check -s exit:0 rump.ifconfig -w 10

	if $DEBUG; then
		rump.netstat -nr
		rump.sysctl net.inet6.ip6.forwarding
	fi
}

setup_forwarding6()
{
	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 -o ignore rump.sysctl -w net.inet6.ip6.forwarding=1
}

setup6()
{

	rump_server_start $SOCKSRC netinet6
	rump_server_start $SOCKFWD netinet6
	rump_server_start $SOCKDST netinet6

	setup_endpoint $SOCKSRC $IP6SRC $BUS_SRCGW $IP6SRCGW
	setup_endpoint $SOCKDST $IP6DST $BUS_DSTGW $IP6DSTGW
	setup_forwarder
}

check_echo_request_pkt()
{
	local pkt="$1 > $2: .+ echo request"

	extract_new_packets $BUS_SRCGW > ./out
	$DEBUG && echo $pkt
	$DEBUG && cat ./out
	atf_check -s exit:0 -o match:"$pkt" cat ./out
}

check_echo_request_pkt_with_macaddr()
{
	local pkt="$1 > $2, .+ $3 > $4: .+ echo request"

	extract_new_packets $BUS_SRCGW > ./out
	$DEBUG && echo $pkt
	$DEBUG && cat ./out
	atf_check -s exit:0 -o match:"$pkt" cat ./out
}

check_echo_request_pkt_with_macaddr_and_rthdr0()
{
	local pkt=

	pkt="$1 > $2, .+ $3 > $4:"
	pkt="$pkt srcrt \\(len=2, type=0, segleft=1, \\[0\\]$5\\)"
	pkt="$pkt .+ echo request"

	extract_new_packets $BUS_SRCGW > ./out
	$DEBUG && echo $pkt
	$DEBUG && cat ./out
	atf_check -s exit:0 -o match:"$pkt" cat ./out
}

#
# Tests
#
atf_test_case ping6_opts_sourceaddr cleanup
ping6_opts_sourceaddr_head()
{

	atf_set "descr" "tests of ping6 -S option"
	atf_set "require.progs" "rump_server"
}

ping6_opts_sourceaddr_body()
{

	setup6
	setup_forwarding6

	export RUMP_SERVER=$SOCKSRC
	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT $IP6DST
	check_echo_request_pkt $IP6SRC $IP6DST

	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $IP6SRC2
	atf_check -s exit:0 rump.ifconfig -w 10

	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT $IP6DST
	check_echo_request_pkt $IP6SRC $IP6DST

	# ping6 -S <sourceaddr>
	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT \
	    -S $IP6SRC $IP6DST
	check_echo_request_pkt $IP6SRC $IP6DST

	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT \
	    -S $IP6SRC2 $IP6DST
	check_echo_request_pkt $IP6SRC2 $IP6DST

	rump_server_destroy_ifaces
}

ping6_opts_sourceaddr_cleanup()
{

	$DEBUG && dump
	cleanup
}

atf_test_case ping6_opts_interface cleanup
ping6_opts_interface_head()
{

	atf_set "descr" "tests of ping6 -I option"
	atf_set "require.progs" "rump_server"
}

ping6_opts_interface_body()
{
	local shmif0_lladdr=
	local shmif1_lladdr=
	local gw_lladdr=

	setup6
	setup_forwarding6

	shmif0_lladdr=$(get_linklocal_addr ${SOCKSRC} shmif0)
	gw_lladdr=$(get_linklocal_addr ${SOCKFWD} shmif0)

	export RUMP_SERVER=$SOCKSRC
	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT $gw_lladdr
	check_echo_request_pkt $shmif0_lladdr $gw_lladdr

	rump_server_add_iface $SOCKSRC shmif1 $BUS_SRCGW
	atf_check -s exit:0 rump.ifconfig shmif1 up
	atf_check -s exit:0 rump.ifconfig -w 10
	shmif1_lladdr=$(get_linklocal_addr ${SOCKSRC} shmif1)

	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT $gw_lladdr
	check_echo_request_pkt $shmif0_lladdr $gw_lladdr

	# ping6 -I <interface>
	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT \
	    -I shmif0 $gw_lladdr
	check_echo_request_pkt $shmif0_lladdr $gw_lladdr

	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT \
	    -I shmif1 $gw_lladdr
	check_echo_request_pkt $shmif1_lladdr $gw_lladdr

	rump_server_destroy_ifaces
}

ping6_opts_interface_cleanup()
{

	$DEBUG && dump
	cleanup
}

atf_test_case ping6_opts_gateway cleanup
ping6_opts_gateway_head()
{

	atf_set "descr" "tests of ping6 -g option"
	atf_set "require.progs" "rump_server"
}

ping6_opts_gateway_body()
{
	local my_macaddr=
	local gw_shmif0_macaddr=
	local gw_shmif2_macaddr=

	setup6
	setup_forwarding6

	my_macaddr=$(get_macaddr ${SOCKSRC} shmif0)
	gw_shmif0_macaddr=$(get_macaddr ${SOCKFWD} shmif0)

	export RUMP_SERVER=$SOCKSRC
	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT $IP6DST
	check_echo_request_pkt_with_macaddr \
	    $my_macaddr $gw_shmif0_macaddr $IP6SRC $IP6DST

	rump_server_add_iface $SOCKFWD shmif2 $BUS_SRCGW
	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 rump.ifconfig shmif2 inet6 $IP6SRCGW2
	atf_check -s exit:0 rump.ifconfig -w 10
	gw_shmif2_macaddr=$(get_macaddr ${SOCKFWD} shmif2)

	export RUMP_SERVER=$SOCKSRC
	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT $IP6DST
	check_echo_request_pkt_with_macaddr \
	    $my_macaddr $gw_shmif0_macaddr $IP6SRC $IP6DST

	# ping6 -g <gateway>
	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT \
	    -g $IP6SRCGW $IP6DST
	check_echo_request_pkt_with_macaddr \
	    $my_macaddr $gw_shmif0_macaddr $IP6SRC $IP6DST

	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT \
	    -g $IP6SRCGW2 $IP6DST
	check_echo_request_pkt_with_macaddr \
	    $my_macaddr $gw_shmif2_macaddr $IP6SRC $IP6DST

	rump_server_destroy_ifaces
}

ping6_opts_gateway_cleanup()
{

	$DEBUG && dump
	cleanup
}

atf_test_case ping6_opts_hops cleanup
ping6_opts_hops_head()
{

	atf_set "descr" "tests of ping6 hops (Type 0 Routing Header)"
	atf_set "require.progs" "rump_server"
}

ping6_opts_hops_body()
{
	local my_macaddr=
	local gw_shmif0_macaddr=
	local gw_shmif2_macaddr=

	setup6
	setup_forwarding6

	my_macaddr=$(get_macaddr ${SOCKSRC} shmif0)
	gw_shmif0_macaddr=$(get_macaddr ${SOCKFWD} shmif0)

	export RUMP_SERVER=$SOCKSRC
	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT $IP6DST
	check_echo_request_pkt_with_macaddr \
	    $my_macaddr $gw_shmif0_macaddr $IP6SRC $IP6DST

	rump_server_add_iface $SOCKFWD shmif2 $BUS_SRCGW
	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 rump.ifconfig shmif2 inet6 $IP6SRCGW2
	atf_check -s exit:0 rump.ifconfig -w 10
	gw_shmif2_macaddr=$(get_macaddr ${SOCKFWD} shmif2)

	export RUMP_SERVER=$SOCKSRC
	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT $IP6DST
	check_echo_request_pkt_with_macaddr \
	    $my_macaddr $gw_shmif0_macaddr $IP6SRC $IP6DST

	# ping6 hops

	# ping6 fails expectedly because the kernel doesn't support
	# to receive packets with type 0 routing headers, but we can
	# check whether a sent packet is correct.
	atf_check -s not-exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT \
	    $IP6SRCGW $IP6DST
	check_echo_request_pkt_with_macaddr_and_rthdr0 \
	    $my_macaddr $gw_shmif0_macaddr $IP6SRC $IP6SRCGW $IP6DST

	atf_check -s not-exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT \
	    $IP6SRCGW2 $IP6DST
	check_echo_request_pkt_with_macaddr_and_rthdr0 \
	    $my_macaddr $gw_shmif2_macaddr $IP6SRC $IP6SRCGW2 $IP6DST

	# ping6 -g <gateway> hops
	atf_check -s not-exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT \
	    -g $IP6SRCGW $IP6SRCGW $IP6DST
	check_echo_request_pkt_with_macaddr_and_rthdr0 \
	    $my_macaddr $gw_shmif0_macaddr $IP6SRC $IP6SRCGW $IP6DST

	atf_check -s not-exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT \
	    -g $IP6SRCGW2 $IP6SRCGW2 $IP6DST
	check_echo_request_pkt_with_macaddr_and_rthdr0 \
	    $my_macaddr $gw_shmif2_macaddr $IP6SRC $IP6SRCGW2 $IP6DST

	# ping6 -g <gateway> hops, but different nexthops (is it valid?)
	atf_check -s not-exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT \
	    -g $IP6SRCGW $IP6SRCGW2 $IP6DST
	check_echo_request_pkt_with_macaddr_and_rthdr0 \
	    $my_macaddr $gw_shmif0_macaddr $IP6SRC $IP6SRCGW2 $IP6DST

	rump_server_destroy_ifaces
}

ping6_opts_hops_cleanup()
{

	$DEBUG && dump
	cleanup
}

atf_init_test_cases()
{

	atf_add_test_case ping6_opts_sourceaddr
	atf_add_test_case ping6_opts_interface
	atf_add_test_case ping6_opts_gateway
	atf_add_test_case ping6_opts_hops
}
