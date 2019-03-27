#	$NetBSD: t_ipv6address.sh,v 1.12 2016/12/14 02:50:42 ozaki-r Exp $
#
# Copyright (c) 2015 Internet Initiative Japan Inc.
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

SERVER="rump_server -lrumpnet -lrumpnet_net -lrumpnet_netinet"
SERVER="${SERVER} -lrumpnet_shmif -lrumpdev"
SERVER6="${SERVER} -lrumpnet_netinet6"

SOCKSRC=unix://commsock1
SOCKFWD=unix://commsock2
SOCKDST=unix://commsock3
IP6SRCNW=fc00:1::0/64
IP6SRC=fc00:1::1
IP6DSTNW=fc00:2::0/64
IP6DST=fc00:2::1
IP6FWD0=fc00:3::1
BUS1=bus1
BUS2=bus2
BUSSRC=bus_src
BUSDST=bus_dst

DEBUG=${DEBUG:-true}
TIMEOUT=3

atf_test_case linklocal cleanup
atf_test_case linklocal_ops cleanup

setup()
{
	atf_check -s exit:0 ${SERVER6} ${SOCKSRC}
	atf_check -s exit:0 ${SERVER6} ${SOCKFWD}
	atf_check -s exit:0 ${SERVER6} ${SOCKDST}

	export RUMP_SERVER=${SOCKSRC}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif1 create
	unset RUMP_SERVER

	export RUMP_SERVER=${SOCKDST}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif1 create
	unset RUMP_SERVER

	export RUMP_SERVER=${SOCKFWD}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif1 create
	atf_check -s exit:0 -o match:"0 -> 1" rump.sysctl \
	    -w net.inet6.ip6.forwarding=1
	unset RUMP_SERVER

	setup_ifcfg

	export RUMP_SERVER=${SOCKSRC}
	$DEBUG && rump.ifconfig
	$DEBUG && rump.netstat -rn -f inet6
	unset RUMP_SERVER

	export RUMP_SERVER=${SOCKDST}
	$DEBUG && rump.ifconfig
	$DEBUG && rump.netstat -rn -f inet6
	unset RUMP_SERVER

	export RUMP_SERVER=${SOCKFWD}
	$DEBUG && rump.ifconfig
	$DEBUG && rump.netstat -rn -f inet6
	unset RUMP_SERVER
}
setup_ifcfg()
{
	export RUMP_SERVER=${SOCKSRC}
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr ${BUS1}
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig shmif1 linkstr ${BUSSRC}
	atf_check -s exit:0 rump.ifconfig shmif1 up
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10
	unset RUMP_SERVER

	export RUMP_SERVER=${SOCKDST}
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr ${BUS2}
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig shmif1 linkstr ${BUSDST}
	atf_check -s exit:0 rump.ifconfig shmif1 up
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10
	unset RUMP_SERVER

	export RUMP_SERVER=${SOCKFWD}
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr ${BUS1}
	atf_check -s exit:0 rump.ifconfig shmif0 up

	atf_check -s exit:0 rump.ifconfig shmif1 linkstr ${BUS2}
	atf_check -s exit:0 rump.ifconfig shmif1 up
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10
	unset RUMP_SERVER
}

setup_route()
{
	local tmp_rump_server=$RUMP_SERVER

	local src_if0_lladdr=`get_linklocal_addr ${SOCKSRC} shmif0`
	local dst_if0_lladdr=`get_linklocal_addr ${SOCKDST} shmif0`
	local fwd_if0_lladdr=`get_linklocal_addr ${SOCKFWD} shmif0`
	local fwd_if1_lladdr=`get_linklocal_addr ${SOCKFWD} shmif1`

	export RUMP_SERVER=${SOCKSRC}
	atf_check -s ignore -o ignore -e ignore \
	    rump.route delete -inet6 default ${fwd_if0_lladdr}%shmif0
	atf_check -s exit:0 -o match:"add net default:" \
	    rump.route add    -inet6 default ${fwd_if0_lladdr}%shmif0
	atf_check -s exit:0 rump.ifconfig shmif1 inet6 ${IP6SRC}
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10
	$DEBUG && rump.netstat -rn -f inet6
	unset RUMP_SERVER

	export RUMP_SERVER=${SOCKDST}
	atf_check -s ignore -o ignore -e ignore \
	    rump.route delete -inet6 default ${fwd_if1_lladdr}%shmif0
	atf_check -s exit:0 -o match:"add net default:" \
	    rump.route add    -inet6 default ${fwd_if1_lladdr}%shmif0
	atf_check -s exit:0 rump.ifconfig shmif1 inet6 ${IP6DST}
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10
	$DEBUG && rump.netstat -rn -f inet6
	unset RUMP_SERVER

	export RUMP_SERVER=${SOCKFWD}
	atf_check -s ignore -o ignore -e ignore \
	    rump.route delete -inet6 ${IP6SRCNW} ${src_if0_lladdr}%shmif0
	atf_check -s exit:0 -o match:"add net" \
	    rump.route add    -inet6 ${IP6SRCNW} ${src_if0_lladdr}%shmif0

	atf_check -s ignore -o ignore -e ignore \
	    rump.route delete -inet6 ${IP6DSTNW} ${dst_if0_lladdr}%shmif1
	atf_check -s exit:0 -o match:"add net" \
	    rump.route add    -inet6 ${IP6DSTNW} ${dst_if0_lladdr}%shmif1
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10
	$DEBUG && rump.netstat -rn -f inet6
	unset RUMP_SERVER

	export RUMP_SERVER=$tmp_rump_server
}

cleanup_bus()
{
	local tmp_rump_server=$RUMP_SERVER

	$DEBUG && dump_bus

	export RUMP_SERVER=${SOCKSRC}
	atf_check -s exit:0 rump.ifconfig shmif0 down
	atf_check -s exit:0 rump.ifconfig shmif0 -linkstr
	atf_check -s exit:0 rump.ifconfig shmif1 down
	atf_check -s exit:0 rump.ifconfig shmif1 -linkstr
	unset RUMP_SERVER

	export RUMP_SERVER=${SOCKDST}
	atf_check -s exit:0 rump.ifconfig shmif0 down
	atf_check -s exit:0 rump.ifconfig shmif0 -linkstr
	atf_check -s exit:0 rump.ifconfig shmif1 down
	atf_check -s exit:0 rump.ifconfig shmif1 -linkstr
	unset RUMP_SERVER

	export RUMP_SERVER=${SOCKFWD}
	atf_check -s exit:0 rump.ifconfig shmif0 down
	atf_check -s exit:0 rump.ifconfig shmif0 -linkstr
	atf_check -s exit:0 rump.ifconfig shmif1 down
	atf_check -s exit:0 rump.ifconfig shmif1 -linkstr
	unset RUMP_SERVER

	atf_check -s exit:0 rm ${BUSSRC}
	atf_check -s exit:0 rm ${BUSDST}
	atf_check -s exit:0 rm ${BUS1}
	atf_check -s exit:0 rm ${BUS2}

	setup_ifcfg

	export RUMP_SERVER=$tmp_rump_server
}

cleanup_rump_servers()
{

	env RUMP_SERVER=${SOCKSRC} rump.halt
	env RUMP_SERVER=${SOCKDST} rump.halt
	env RUMP_SERVER=${SOCKFWD} rump.halt
}

dump_bus()
{

	shmif_dumpbus -p - ${BUSSRC} 2>/dev/null| tcpdump -n -e -r -
	shmif_dumpbus -p - ${BUSDST} 2>/dev/null| tcpdump -n -e -r -
	shmif_dumpbus -p - ${BUS1}   2>/dev/null| tcpdump -n -e -r -
	shmif_dumpbus -p - ${BUS2}   2>/dev/null| tcpdump -n -e -r -
}

_dump()
{

	export RUMP_SERVER=${SOCKSRC}
	rump.ndp -n -a
	rump.netstat -nr -f inet6
	export RUMP_SERVER=${SOCKDST}
	rump.ndp -n -a
	rump.netstat -nr -f inet6
	export RUMP_SERVER=${SOCKFWD}
	rump.ndp -n -a
	rump.netstat -nr -f inet6
	unset RUMP_SERVER
}

linklocal_head()
{
	atf_set "descr" \
	    "Test for bassically function of the IPv6 linklocal address"
	atf_set "require.progs" \
	    "rump_server rump.route rump.ifconfig rump.ping6"
}

linklocal_body()
{
	setup

	local src_if0_lladdr=`get_linklocal_addr ${SOCKSRC} shmif0`
	local src_if1_lladdr=`get_linklocal_addr ${SOCKSRC} shmif1`
	local dst_if0_lladdr=`get_linklocal_addr ${SOCKDST} shmif0`
	local fwd_if0_lladdr=`get_linklocal_addr ${SOCKFWD} shmif0`
	local fwd_if1_lladdr=`get_linklocal_addr ${SOCKFWD} shmif1`

	export RUMP_SERVER=${SOCKSRC}
	$DEBUG && rump.ifconfig
	$DEBUG && rump.netstat -rn -f inet6

	# link local address to link local address

	atf_check -s not-exit:0 -e match:"No route to host" \
	    rump.ping6 -c 1 -X $TIMEOUT -n ${fwd_if0_lladdr}

	atf_check -s exit:0 -o match:"0.0% packet loss" \
	    rump.ping6 -c 1 -X $TIMEOUT -n ${fwd_if0_lladdr}%shmif0

	atf_check -s ignore -o empty -e ignore \
	    -x "shmif_dumpbus -p - ${BUSSRC} | tcpdump -r - -n -p icmp6"
	atf_check -s ignore -o not-empty -e ignore \
	    -x "shmif_dumpbus -p - ${BUS1}   | tcpdump -r - -n -p icmp6"

	cleanup_bus

	atf_check -s not-exit:0 -o ignore -e ignore \
	    rump.ping6 -c 1 -X $TIMEOUT -n -S ${src_if1_lladdr}%shmif1 \
	    ${fwd_if0_lladdr}%shmif0
	atf_check -s ignore -o not-match:"${src_if1_lladdr}" -e ignore \
	    -x   "shmif_dumpbus -p - ${BUS1} | tcpdump -r - -n -p icmp6"
	$DEBUG && shmif_dumpbus -p - ${BUS1} | tcpdump -r - -n -p icmp6
	unset RUMP_SERVER

	# link local address to host address
	export RUMP_SERVER=${SOCKFWD}
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 ${IP6FWD0}
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10
	unset RUMP_SERVER

	export RUMP_SERVER=${SOCKSRC}
	atf_check -s exit:0 -o match:"add net default:" \
	    rump.route add -inet6 default ${fwd_if0_lladdr}%shmif0
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10

	$DEBUG && rump.ifconfig shmif0
	$DEBUG && _dump

	export RUMP_SERVER=${SOCKSRC}
	atf_check -s exit:0 -o match:"0.0% packet loss" \
	  rump.ping6 -c 1 -X $TIMEOUT -n -S ${src_if0_lladdr}%shmif0 ${IP6FWD0}
	unset RUMP_SERVER

	export RUMP_SERVER=${SOCKFWD}
	# host address to link local address
	atf_check -s exit:0 -o match:"0.0% packet loss" \
	    rump.ping6 -c 1 -X $TIMEOUT -n ${src_if0_lladdr}%shmif0
	atf_check -s not-exit:0 -o match:"100.0% packet loss" \
	    rump.ping6 -c 1 -X $TIMEOUT -n ${src_if1_lladdr}%shmif0

	atf_check -s exit:0 rump.ifconfig shmif0 inet6 ${IP6FWD0} delete

	unset RUMP_SERVER

	# forwarding with link local address
	setup_route

	export RUMP_SERVER=${SOCKSRC}
	atf_check -s exit:0 -o match:"0.0% packet loss" rump.ping6 -c 1 \
	    -X $TIMEOUT -n -S ${IP6SRC} ${IP6DST}

	cleanup_bus
	$DEBUG && rump.ifconfig shmif0
	atf_check -s not-exit:0 -o match:"100.0% packet loss" rump.ping6 -c 1 \
	    -X $TIMEOUT -n -S ${src_if0_lladdr}%shmif0 ${IP6DST}
	atf_check -s ignore -o not-match:"${src_if0_lladdr}" -e ignore \
	    -x "shmif_dumpbus -p - ${BUS2} | tcpdump -r - -n -p icmp6"

	cleanup_bus
	atf_check -s not-exit:0 -o match:"100.0% packet loss" rump.ping6 -c 1 \
	    -X $TIMEOUT -n -S ${IP6SRC} ${dst_if0_lladdr}%shmif0
	atf_check -s ignore -o not-empty -e ignore \
	    -x "shmif_dumpbus -p - ${BUS2} | tcpdump -r - -n -p icmp6"

	unset RUMP_SERVER

}

linklocal_cleanup()
{

	$DEBUG && _dump
	$DEBUG && dump_bus
	cleanup_rump_servers
}

linklocal_ops_head()
{

	atf_set "descr" \
	    "Test for various operations to IPv6 linklocal addresses"
	atf_set "require.progs" "rump_server rump.route rump.ndp"
}

linklocal_ops_body()
{
	local src_if0_lladdr=

	setup

	src_if0_lladdr=`get_linklocal_addr ${SOCKSRC} shmif0`

	export RUMP_SERVER=${SOCKSRC}

	# route get
	atf_check -s exit:0 -o match:"${src_if0_lladdr}" \
	    rump.route get -inet6 ${src_if0_lladdr}%shmif0

	# route get without an interface name (zone index)
	atf_check -s not-exit:0 -e match:"not in table" \
	    rump.route get -inet6 ${src_if0_lladdr}

	# ndp
	atf_check -s exit:0 -o match:"${src_if0_lladdr}" \
	    rump.ndp -n ${src_if0_lladdr}%shmif0

	# ndp without an interface name (zone index)
	atf_check -s not-exit:0 -o ignore -e match:"no entry" \
	    rump.ndp -n ${src_if0_lladdr}
}


linklocal_ops_cleanup()
{

	cleanup_rump_servers
}

atf_init_test_cases()
{

	atf_add_test_case linklocal
	atf_add_test_case linklocal_ops
}
