#	$NetBSD: t_arp.sh,v 1.22 2016/11/25 08:51:16 ozaki-r Exp $
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

SOCKSRC=unix://commsock1
SOCKDST=unix://commsock2
IP4SRC=10.0.1.1
IP4DST=10.0.1.2
IP4DST_PROXYARP1=10.0.1.3
IP4DST_PROXYARP2=10.0.1.4

DEBUG=${DEBUG:-false}
TIMEOUT=1

atf_test_case arp_cache_expiration_5s cleanup
atf_test_case arp_cache_expiration_10s cleanup
atf_test_case arp_command cleanup
atf_test_case arp_garp cleanup
atf_test_case arp_cache_overwriting cleanup
atf_test_case arp_proxy_arp_pub cleanup
atf_test_case arp_proxy_arp_pubproxy cleanup
atf_test_case arp_link_activation cleanup
atf_test_case arp_static cleanup

arp_cache_expiration_5s_head()
{
	atf_set "descr" "Tests for ARP cache expiration (5s)"
	atf_set "require.progs" "rump_server"
}

arp_cache_expiration_10s_head()
{
	atf_set "descr" "Tests for ARP cache expiration (10s)"
	atf_set "require.progs" "rump_server"
}

arp_command_head()
{
	atf_set "descr" "Tests for arp_commands of arp(8)"
	atf_set "require.progs" "rump_server"
}

arp_garp_head()
{
	atf_set "descr" "Tests for GARP"
	atf_set "require.progs" "rump_server"
}

arp_cache_overwriting_head()
{
	atf_set "descr" "Tests for behavior of overwriting ARP caches"
	atf_set "require.progs" "rump_server"
}

arp_proxy_arp_pub_head()
{
	atf_set "descr" "Tests for Proxy ARP (pub)"
	atf_set "require.progs" "rump_server"
}

arp_proxy_arp_pubproxy_head()
{
	atf_set "descr" "Tests for Proxy ARP (pub proxy)"
	atf_set "require.progs" "rump_server"
}

arp_link_activation_head()
{
	atf_set "descr" "Tests for activating a new MAC address"
	atf_set "require.progs" "rump_server"
}

arp_static_head()
{

	atf_set "descr" "Tests for static ARP entries"
	atf_set "require.progs" "rump_server"
}

setup_dst_server()
{

	rump_server_add_iface $SOCKDST shmif0 bus1
	export RUMP_SERVER=$SOCKDST
	atf_check -s exit:0 rump.ifconfig shmif0 inet $IP4DST/24
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig -w 10

	$DEBUG && rump.ifconfig shmif0
	$DEBUG && rump.arp -n -a
}

setup_src_server()
{
	local keep=$1

	export RUMP_SERVER=$SOCKSRC

	# Adjust ARP parameters
	atf_check -s exit:0 -o ignore rump.sysctl -w net.inet.arp.keep=$keep

	# Setup an interface
	rump_server_add_iface $SOCKSRC shmif0 bus1
	atf_check -s exit:0 rump.ifconfig shmif0 inet $IP4SRC/24
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig -w 10

	# Sanity check
	$DEBUG && rump.ifconfig shmif0
	$DEBUG && rump.arp -n -a
	atf_check -s exit:0 -o ignore rump.arp -n $IP4SRC
	atf_check -s not-exit:0 -e ignore rump.arp -n $IP4DST
}

test_cache_expiration()
{
	local arp_keep=$1
	local bonus=2

	rump_server_start $SOCKSRC
	rump_server_start $SOCKDST

	setup_dst_server
	setup_src_server $arp_keep

	#
	# Check if a cache is expired expectedly
	#
	export RUMP_SERVER=$SOCKSRC
	atf_check -s exit:0 -o ignore rump.ping -n -w $TIMEOUT -c 1 $IP4DST

	$DEBUG && rump.arp -n -a
	atf_check -s exit:0 -o ignore rump.arp -n $IP4SRC
	# Should be cached
	atf_check -s exit:0 -o ignore rump.arp -n $IP4DST

	atf_check -s exit:0 sleep $(($arp_keep + $bonus))

	$DEBUG && rump.arp -n -a
	atf_check -s exit:0 -o ignore rump.arp -n $IP4SRC
	# Should be expired
	atf_check -s not-exit:0 -e ignore rump.arp -n $IP4DST
}

arp_cache_expiration_5s_body()
{

	test_cache_expiration 5
	rump_server_destroy_ifaces
}

arp_cache_expiration_10s_body()
{

	test_cache_expiration 10
	rump_server_destroy_ifaces
}

arp_command_body()
{
	local arp_keep=5
	local bonus=2

	rump_server_start $SOCKSRC
	rump_server_start $SOCKDST

	setup_dst_server
	setup_src_server $arp_keep

	export RUMP_SERVER=$SOCKSRC

	# We can delete the entry for the interface's IP address
	atf_check -s exit:0 -o ignore rump.arp -d $IP4SRC

	# Add and delete a static entry
	$DEBUG && rump.arp -n -a
	atf_check -s exit:0 -o ignore rump.arp -s 10.0.1.10 b2:a0:20:00:00:10
	$DEBUG && rump.arp -n -a
	atf_check -s exit:0 -o match:'b2:a0:20:00:00:10' rump.arp -n 10.0.1.10
	atf_check -s exit:0 -o match:'permanent' rump.arp -n 10.0.1.10
	atf_check -s exit:0 -o ignore rump.arp -d 10.0.1.10
	$DEBUG && rump.arp -n -a
	atf_check -s not-exit:0 -e ignore rump.arp -n 10.0.1.10

	# Add multiple entries via a file
	cat - > ./list <<-EOF
	10.0.1.11 b2:a0:20:00:00:11
	10.0.1.12 b2:a0:20:00:00:12
	10.0.1.13 b2:a0:20:00:00:13
	10.0.1.14 b2:a0:20:00:00:14
	10.0.1.15 b2:a0:20:00:00:15
	EOF
	$DEBUG && rump.arp -n -a
	atf_check -s exit:0 -o ignore rump.arp -f ./list
	$DEBUG && rump.arp -n -a
	atf_check -s exit:0 -o match:'b2:a0:20:00:00:11' rump.arp -n 10.0.1.11
	atf_check -s exit:0 -o match:'permanent' rump.arp -n 10.0.1.11
	atf_check -s exit:0 -o match:'b2:a0:20:00:00:12' rump.arp -n 10.0.1.12
	atf_check -s exit:0 -o match:'permanent' rump.arp -n 10.0.1.12
	atf_check -s exit:0 -o match:'b2:a0:20:00:00:13' rump.arp -n 10.0.1.13
	atf_check -s exit:0 -o match:'permanent' rump.arp -n 10.0.1.13
	atf_check -s exit:0 -o match:'b2:a0:20:00:00:14' rump.arp -n 10.0.1.14
	atf_check -s exit:0 -o match:'permanent' rump.arp -n 10.0.1.14
	atf_check -s exit:0 -o match:'b2:a0:20:00:00:15' rump.arp -n 10.0.1.15
	atf_check -s exit:0 -o match:'permanent' rump.arp -n 10.0.1.15

	# Test arp -a
	atf_check -s exit:0 -o match:'10.0.1.11' rump.arp -n -a
	atf_check -s exit:0 -o match:'10.0.1.12' rump.arp -n -a
	atf_check -s exit:0 -o match:'10.0.1.13' rump.arp -n -a
	atf_check -s exit:0 -o match:'10.0.1.14' rump.arp -n -a
	atf_check -s exit:0 -o match:'10.0.1.15' rump.arp -n -a

	# Flush all entries
	$DEBUG && rump.arp -n -a
	atf_check -s exit:0 -o ignore rump.arp -d -a
	atf_check -s not-exit:0 -e ignore rump.arp -n 10.0.1.11
	atf_check -s not-exit:0 -e ignore rump.arp -n 10.0.1.12
	atf_check -s not-exit:0 -e ignore rump.arp -n 10.0.1.13
	atf_check -s not-exit:0 -e ignore rump.arp -n 10.0.1.14
	atf_check -s not-exit:0 -e ignore rump.arp -n 10.0.1.15
	atf_check -s not-exit:0 -e ignore rump.arp -n 10.0.1.1

	# Test temp option
	$DEBUG && rump.arp -n -a
	atf_check -s exit:0 -o ignore rump.arp -s 10.0.1.10 b2:a0:20:00:00:10 temp
	$DEBUG && rump.arp -n -a
	atf_check -s exit:0 -o match:'b2:a0:20:00:00:10' rump.arp -n 10.0.1.10
	atf_check -s exit:0 -o not-match:'permanent' rump.arp -n 10.0.1.10

	# Hm? the cache doesn't expire...
	atf_check -s exit:0 sleep $(($arp_keep + $bonus))
	$DEBUG && rump.arp -n -a
	#atf_check -s not-exit:0 -e ignore rump.arp -n 10.0.1.10

	rump_server_destroy_ifaces
}

make_pkt_str_arpreq()
{
	local target=$1
	local sender=$2
	pkt="> ff:ff:ff:ff:ff:ff, ethertype ARP (0x0806), length 42:"
	pkt="$pkt Request who-has $target tell $sender, length 28"
	echo $pkt
}

arp_garp_body()
{
	local pkt=

	rump_server_start $SOCKSRC

	export RUMP_SERVER=$SOCKSRC

	# Setup an interface
	rump_server_add_iface $SOCKSRC shmif0 bus1
	atf_check -s exit:0 rump.ifconfig shmif0 inet 10.0.0.1/24
	atf_check -s exit:0 rump.ifconfig shmif0 inet 10.0.0.2/24 alias
	atf_check -s exit:0 rump.ifconfig shmif0 up
	$DEBUG && rump.ifconfig shmif0

	atf_check -s exit:0 sleep 1
	shmif_dumpbus -p - bus1 2>/dev/null| tcpdump -n -e -r - > ./out

	# A GARP packet is sent for the primary address
	pkt=$(make_pkt_str_arpreq 10.0.0.1 10.0.0.1)
	atf_check -s exit:0 -x "cat ./out |grep -q '$pkt'"
	# No GARP packet is sent for the alias address
	pkt=$(make_pkt_str_arpreq 10.0.0.2 10.0.0.2)
	atf_check -s not-exit:0 -x "cat ./out |grep -q '$pkt'"

	atf_check -s exit:0 rump.ifconfig -w 10
	atf_check -s exit:0 rump.ifconfig shmif0 inet 10.0.0.3/24
	atf_check -s exit:0 rump.ifconfig shmif0 inet 10.0.0.4/24 alias

	# No GARP packets are sent during IFF_UP
	shmif_dumpbus -p - bus1 2>/dev/null| tcpdump -n -e -r - > ./out
	pkt=$(make_pkt_str_arpreq 10.0.0.3 10.0.0.3)
	atf_check -s not-exit:0 -x "cat ./out |grep -q '$pkt'"
	pkt=$(make_pkt_str_arpreq 10.0.0.4 10.0.0.4)
	atf_check -s not-exit:0 -x "cat ./out |grep -q '$pkt'"

	rump_server_destroy_ifaces
}

arp_cache_overwriting_body()
{
	local arp_keep=5
	local bonus=2

	rump_server_start $SOCKSRC
	rump_server_start $SOCKDST

	setup_dst_server
	setup_src_server $arp_keep

	export RUMP_SERVER=$SOCKSRC

	# Cannot overwrite a permanent cache
	atf_check -s not-exit:0 -e match:'File exists' \
	    rump.arp -s $IP4SRC b2:a0:20:00:00:ff
	$DEBUG && rump.arp -n -a

	atf_check -s exit:0 -o ignore rump.ping -n -w $TIMEOUT -c 1 $IP4DST
	$DEBUG && rump.arp -n -a
	# Can overwrite a dynamic cache
	atf_check -s exit:0 -o ignore rump.arp -s $IP4DST b2:a0:20:00:00:00
	$DEBUG && rump.arp -n -a
	atf_check -s exit:0 -o match:'b2:a0:20:00:00:00' rump.arp -n $IP4DST
	atf_check -s exit:0 -o match:'permanent' rump.arp -n $IP4DST

	atf_check -s exit:0 -o ignore rump.arp -s 10.0.1.10 b2:a0:20:00:00:10 temp
	$DEBUG && rump.arp -n -a
	atf_check -s exit:0 -o match:'b2:a0:20:00:00:10' rump.arp -n 10.0.1.10
	atf_check -s exit:0 -o not-match:'permanent' rump.arp -n 10.0.1.10
	# Can overwrite a temp cache
	atf_check -s exit:0 -o ignore rump.arp -s 10.0.1.10 b2:a0:20:00:00:ff
	atf_check -s exit:0 -o match:'b2:a0:20:00:00:ff' rump.arp -n 10.0.1.10
	$DEBUG && rump.arp -n -a

	rump_server_destroy_ifaces
}

make_pkt_str_arprep()
{
	local ip=$1
	local mac=$2
	pkt="ethertype ARP (0x0806), length 42: "
	pkt="Reply $ip is-at $mac, length 28"
	echo $pkt
}

make_pkt_str_garp()
{
	local ip=$1
	local mac=$2
	local pkt=
	pkt="$mac > ff:ff:ff:ff:ff:ff, ethertype ARP (0x0806),"
	pkt="$pkt length 42: Request who-has $ip tell $ip, length 28"
	echo $pkt
}

test_proxy_arp()
{
	local arp_keep=5
	local opts= title= flags=
	local type=$1

	rump_server_start $SOCKSRC
	rump_server_start $SOCKDST tap

	setup_dst_server
	setup_src_server $arp_keep

	export RUMP_SERVER=$SOCKDST
	atf_check -s exit:0 -o ignore rump.sysctl -w net.inet.ip.forwarding=1
	macaddr_dst=$(get_macaddr $SOCKDST shmif0)

	if [ "$type" = "pub" ]; then
		opts="pub"
		title="permanent published"
	else
		opts="pub proxy"
		title='permanent published \(proxy only\)'
	fi

	#
	# Test#1: First setup an endpoint then create proxy arp entry
	#
	export RUMP_SERVER=$SOCKDST
	atf_check -s exit:0 rump.ifconfig tap1 create
	atf_check -s exit:0 rump.ifconfig tap1 $IP4DST_PROXYARP1/24 up
	atf_check -s exit:0 rump.ifconfig -w 10

	# Try to ping (should fail w/o proxy arp)
	export RUMP_SERVER=$SOCKSRC
	atf_check -s not-exit:0 -o ignore -e ignore \
	    rump.ping -n -w 1 -c 1 $IP4DST_PROXYARP1

	# Flushing
	extract_new_packets bus1 > ./out

	# Set up proxy ARP entry
	export RUMP_SERVER=$SOCKDST
	atf_check -s exit:0 -o ignore \
	    rump.arp -s $IP4DST_PROXYARP1 $macaddr_dst $opts
	atf_check -s exit:0 -o match:"$title" rump.arp -n $IP4DST_PROXYARP1

	# Try to ping
	export RUMP_SERVER=$SOCKSRC
	if [ "$type" = "pub" ]; then
		# XXX fails
		atf_check -s not-exit:0 -o ignore -e ignore \
		    rump.ping -n -w 1 -c 1 $IP4DST_PROXYARP1
	else
		atf_check -s exit:0 -o ignore \
		    rump.ping -n -w 1 -c 1 $IP4DST_PROXYARP1
	fi

	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out

	pkt1=$(make_pkt_str_arprep $IP4DST_PROXYARP1 $macaddr_dst)
	pkt2=$(make_pkt_str_garp $IP4DST_PROXYARP1 $macaddr_dst)
	if [ "$type" = "pub" ]; then
		atf_check -s not-exit:0 -x \
		    "cat ./out |grep -q -e '$pkt1' -e '$pkt2'"
	else
		atf_check -s exit:0 -x "cat ./out |grep -q -e '$pkt1' -e '$pkt2'"
	fi

	#
	# Test#2: Create proxy arp entry then set up an endpoint
	#
	export RUMP_SERVER=$SOCKDST
	atf_check -s exit:0 -o ignore \
	    rump.arp -s $IP4DST_PROXYARP2 $macaddr_dst $opts
	atf_check -s exit:0 -o match:"$title" rump.arp -n $IP4DST_PROXYARP2
	$DEBUG && rump.netstat -nr -f inet

	# Try to ping (should fail because no endpoint exists)
	export RUMP_SERVER=$SOCKSRC
	atf_check -s not-exit:0 -o ignore -e ignore \
	    rump.ping -n -w 1 -c 1 $IP4DST_PROXYARP2

	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out

	# ARP reply should be sent
	pkt=$(make_pkt_str_arprep $IP4DST_PROXYARP2 $macaddr_dst)
	atf_check -s exit:0 -x "cat ./out |grep -q '$pkt'"

	export RUMP_SERVER=$SOCKDST
	atf_check -s exit:0 rump.ifconfig tap2 create
	atf_check -s exit:0 rump.ifconfig tap2 $IP4DST_PROXYARP2/24 up
	atf_check -s exit:0 rump.ifconfig -w 10

	# Try to ping
	export RUMP_SERVER=$SOCKSRC
	atf_check -s exit:0 -o ignore rump.ping -n -w 1 -c 1 $IP4DST_PROXYARP2
}

arp_proxy_arp_pub_body()
{

	test_proxy_arp pub
	rump_server_destroy_ifaces
}

arp_proxy_arp_pubproxy_body()
{

	test_proxy_arp pubproxy
	rump_server_destroy_ifaces
}

arp_link_activation_body()
{
	local arp_keep=5
	local bonus=2

	rump_server_start $SOCKSRC
	rump_server_start $SOCKDST

	setup_dst_server
	setup_src_server $arp_keep

	# flush old packets
	extract_new_packets bus1 > ./out

	export RUMP_SERVER=$SOCKSRC

	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 link \
	    b2:a1:00:00:00:01

	atf_check -s exit:0 sleep 1
	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out

	pkt=$(make_pkt_str_arpreq $IP4SRC $IP4SRC)
	atf_check -s not-exit:0 -x "cat ./out |grep -q '$pkt'"

	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 link \
	    b2:a1:00:00:00:02 active

	atf_check -s exit:0 sleep 1
	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out

	pkt=$(make_pkt_str_arpreq $IP4SRC $IP4SRC)
	atf_check -s exit:0 -x \
	    "cat ./out |grep '$pkt' |grep -q 'b2:a1:00:00:00:02'"

	rump_server_destroy_ifaces
}

arp_static_body()
{
	local arp_keep=5
	local macaddr_src=

	rump_server_start $SOCKSRC
	rump_server_start $SOCKDST

	setup_dst_server
	setup_src_server $arp_keep

	macaddr_src=$(get_macaddr $SOCKSRC shmif0)

	# Set a (valid) static ARP entry for the src server
	export RUMP_SERVER=$SOCKDST
	$DEBUG && rump.arp -n -a
	atf_check -s exit:0 -o ignore rump.arp -s $IP4SRC $macaddr_src
	$DEBUG && rump.arp -n -a

	# Test receiving an ARP request with the static ARP entry (as spa/sha)
	export RUMP_SERVER=$SOCKSRC
	atf_check -s exit:0 -o ignore rump.ping -n -w 1 -c 1 $IP4DST

	rump_server_destroy_ifaces
}

arp_cache_expiration_5s_cleanup()
{
	$DEBUG && dump
	cleanup
}

arp_cache_expiration_10s_cleanup()
{
	$DEBUG && dump
	cleanup
}

arp_command_cleanup()
{
	$DEBUG && dump
	cleanup
}

arp_garp_cleanup()
{
	$DEBUG && dump
	cleanup
}

arp_cache_overwriting_cleanup()
{
	$DEBUG && dump
	cleanup
}

arp_proxy_arp_pub_cleanup()
{
	$DEBUG && dump
	cleanup
}

arp_proxy_arp_pubproxy_cleanup()
{
	$DEBUG && dump
	cleanup
}

arp_link_activation_cleanup()
{
	$DEBUG && dump
	cleanup
}

arp_static_cleanup()
{
	$DEBUG && dump
	cleanup
}

atf_init_test_cases()
{
	atf_add_test_case arp_cache_expiration_5s
	atf_add_test_case arp_cache_expiration_10s
	atf_add_test_case arp_command
	atf_add_test_case arp_garp
	atf_add_test_case arp_cache_overwriting
	atf_add_test_case arp_proxy_arp_pub
	atf_add_test_case arp_proxy_arp_pubproxy
	atf_add_test_case arp_link_activation
	atf_add_test_case arp_static
}
