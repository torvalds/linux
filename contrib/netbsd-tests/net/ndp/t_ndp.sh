#	$NetBSD: t_ndp.sh,v 1.17 2016/11/25 08:51:17 ozaki-r Exp $
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
IP6SRC=fc00::1
IP6DST=fc00::2

DEBUG=${DEBUG:-true}
TIMEOUT=1

atf_test_case ndp_cache_expiration cleanup
atf_test_case ndp_commands cleanup
atf_test_case ndp_cache_overwriting cleanup
atf_test_case ndp_neighborgcthresh cleanup
atf_test_case ndp_link_activation cleanup

ndp_cache_expiration_head()
{
	atf_set "descr" "Tests for NDP cache expiration"
	atf_set "require.progs" "rump_server"
}

ndp_commands_head()
{
	atf_set "descr" "Tests for commands of ndp(8)"
	atf_set "require.progs" "rump_server"
}

ndp_cache_overwriting_head()
{
	atf_set "descr" "Tests for behavior of overwriting NDP caches"
	atf_set "require.progs" "rump_server"
}

ndp_neighborgcthresh_head()
{
	atf_set "descr" "Tests for GC of neighbor caches"
	atf_set "require.progs" "rump_server"
}

ndp_link_activation_head()
{
	atf_set "descr" "Tests for activating a new MAC address"
	atf_set "require.progs" "rump_server"
}

setup_dst_server()
{
	local assign_ip=$1

	rump_server_add_iface $SOCKDST shmif0 bus1
	export RUMP_SERVER=$SOCKDST
	if [ "$assign_ip" != no ]; then
		atf_check -s exit:0 rump.ifconfig shmif0 inet6 $IP6DST
	fi
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig -w 10

	$DEBUG && rump.ifconfig shmif0
	$DEBUG && rump.ndp -n -a
}

setup_src_server()
{
	$DEBUG && ulimit -c unlimited
	export RUMP_SERVER=$SOCKSRC

	# Setup an interface
	rump_server_add_iface $SOCKSRC shmif0 bus1
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $IP6SRC
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig -w 10

	# Sanity check
	$DEBUG && rump.ifconfig shmif0
	$DEBUG && rump.ndp -n -a
	atf_check -s exit:0 -o ignore rump.ndp -n $IP6SRC
	atf_check -s not-exit:0 -o ignore -e ignore rump.ndp -n $IP6DST
}

get_timeout()
{
	local timeout=$(env RUMP_SERVER=$SOCKSRC rump.ndp -n $IP6DST |grep $IP6DST|awk '{print $4;}')
	timeout=${timeout%s}
	echo $timeout
}

ndp_cache_expiration_body()
{

	rump_server_start $SOCKSRC netinet6
	rump_server_start $SOCKDST netinet6

	setup_dst_server
	setup_src_server

	#
	# Check if a cache is expired expectedly
	#
	export RUMP_SERVER=$SOCKSRC
	atf_check -s exit:0 -o ignore rump.ping6 -n -X $TIMEOUT -c 1 $IP6DST

	$DEBUG && rump.ndp -n -a
	atf_check -s exit:0 -o match:'permanent' rump.ndp -n $IP6SRC
	# Should be cached
	atf_check -s exit:0 -o not-match:'permanent' rump.ndp -n $IP6DST

	timeout=$(get_timeout $IP6DST)

	atf_check -s exit:0 sleep $(($timeout + 1))

	$DEBUG && rump.ndp -n -a
	atf_check -s exit:0 -o match:'permanent' rump.ndp -n $IP6SRC
	# Expired but remains until GC sweaps it (1 day)
	atf_check -s exit:0 -o match:'(1d0h0m|23h59m)' rump.ndp -n $IP6DST

	rump_server_destroy_ifaces
}

ifdown_dst_server()
{
	export RUMP_SERVER=$SOCKDST
	atf_check -s exit:0 rump.ifconfig shmif0 down
	export RUMP_SERVER=$SOCKSRC
}

ndp_commands_body()
{

	rump_server_start $SOCKSRC netinet6
	rump_server_start $SOCKDST netinet6

	setup_dst_server
	setup_src_server

	export RUMP_SERVER=$SOCKSRC

	# We can delete the entry for the interface's IP address
	atf_check -s exit:0 -o match:"$IP6SRC" rump.ndp -d $IP6SRC

	# Add and delete a static entry
	$DEBUG && rump.ndp -n -a
	atf_check -s exit:0 -o ignore rump.ndp -s fc00::10 b2:a0:20:00:00:10
	$DEBUG && rump.ndp -n -a
	atf_check -s exit:0 -o match:'permanent' rump.ndp -n fc00::10
	atf_check -s exit:0 -o match:'deleted' rump.ndp -d fc00::10
	$DEBUG && rump.ndp -n -a
	atf_check -s not-exit:0 -o ignore -e ignore rump.ndp -n fc00::10

	# Add multiple entries via a file (XXX not implemented)
	#cat - > ./list <<-EOF
	#fc00::11 b2:a0:20:00:00:11
	#fc00::12 b2:a0:20:00:00:12
	#fc00::13 b2:a0:20:00:00:13
	#fc00::14 b2:a0:20:00:00:14
	#fc00::15 b2:a0:20:00:00:15
	#EOF
	#$DEBUG && rump.ndp -n -a
	#atf_check -s exit:0 -o ignore rump.ndp -f ./list
	#$DEBUG && rump.ndp -n -a

	atf_check -s exit:0 -o ignore rump.ping6 -n -X $TIMEOUT -c 1 $IP6DST
	atf_check -s exit:0 -o ignore rump.ndp -s fc00::11 b2:a0:20:00:00:11
	atf_check -s exit:0 -o ignore rump.ndp -s fc00::12 b2:a0:20:00:00:12

	atf_check -s exit:0 -o not-match:'permanent' rump.ndp -n $IP6DST
	atf_check -s exit:0 -o match:'permanent' rump.ndp -n fc00::11
	atf_check -s exit:0 -o match:'permanent' rump.ndp -n fc00::12

	# Test ndp -a
	atf_check -s exit:0 -o match:'fc00::11' rump.ndp -n -a
	atf_check -s exit:0 -o match:'fc00::12' rump.ndp -n -a

	# Ensure no packet upsets the src server
	ifdown_dst_server

	# Flush all entries (-c)
	$DEBUG && rump.ndp -n -a
	atf_check -s exit:0 -o ignore rump.ndp -c
	atf_check -s not-exit:0 -o ignore -e ignore rump.ndp -n $IP6SRC
	atf_check -s not-exit:0 -o ignore -e ignore rump.ndp -n $IP6DST
	# Only the static caches are not deleted
	atf_check -s exit:0 -o ignore -e ignore rump.ndp -n fc00::11
	atf_check -s exit:0 -o ignore -e ignore rump.ndp -n fc00::12

	$DEBUG && rump.ndp -n -a
	atf_check -s exit:0 -o ignore rump.ndp -s fc00::10 b2:a0:20:00:00:10 temp
	rump.ndp -s fc00::10 b2:a0:20:00:00:10 temp
	$DEBUG && rump.ndp -n -a
	atf_check -s exit:0 -o not-match:'permanent' rump.ndp -n fc00::10

	rump_server_destroy_ifaces
}

ndp_cache_overwriting_body()
{

	rump_server_start $SOCKSRC netinet6
	rump_server_start $SOCKDST netinet6

	setup_dst_server
	setup_src_server

	export RUMP_SERVER=$SOCKSRC

	# Cannot overwrite a permanent cache
	atf_check -s not-exit:0 -e ignore rump.ndp -s $IP6SRC b2:a0:20:00:00:ff
	$DEBUG && rump.ndp -n -a

	atf_check -s exit:0 -o ignore rump.ping6 -n -X $TIMEOUT -c 1 $IP6DST
	$DEBUG && rump.ndp -n -a
	# Can overwrite a dynamic cache
	atf_check -s exit:0 -o ignore rump.ndp -s $IP6DST b2:a0:20:00:00:00
	$DEBUG && rump.ndp -n -a
	atf_check -s exit:0 -o match:'permanent' rump.ndp -n $IP6DST

	# Test temp option (XXX it doesn't work; expire time isn't set)
	#atf_check -s exit:0 -o ignore rump.ndp -s fc00::10 b2:a0:20:00:00:10 temp
	#$DEBUG && rump.ndp -n -a
	#atf_check -s exit:0 -o not-match:'permanent' rump.ndp -n fc00::10
	# Cannot overwrite a temp cache
	#atf_check -s not-exit:0 -e ignore rump.ndp -s fc00::10 b2:a0:20:00:00:ff
	#$DEBUG && rump.ndp -n -a

	rump_server_destroy_ifaces
}

get_n_caches()
{

	echo $(rump.ndp -a -n |grep -v -e Neighbor -e permanent |wc -l)
}

ndp_neighborgcthresh_body()
{

	rump_server_start $SOCKSRC netinet6
	rump_server_start $SOCKDST netinet6

	setup_dst_server no
	setup_src_server

	export RUMP_SERVER=$SOCKDST
	for i in $(seq 0 9); do
		atf_check -s exit:0 rump.ifconfig shmif0 inet6 ${IP6DST}$i
	done

	export RUMP_SERVER=$SOCKSRC

	# ping to 3 destinations
	$DEBUG && rump.ndp -n -a
	for i in $(seq 0 2); do
		atf_check -s exit:0 -o ignore rump.ping6 -n -X $TIMEOUT -c 1 \
		    ${IP6DST}$i
	done
	$DEBUG && rump.ndp -n -a

	# 3 caches should be created
	atf_check_equal $(get_n_caches) 3

	# ping to additional 3 destinations
	for i in $(seq 3 5); do
		atf_check -s exit:0 -o ignore rump.ping6 -n -X $TIMEOUT -c 1 \
		    ${IP6DST}$i
	done
	$DEBUG && rump.ndp -n -a

	# 6 caches should be created in total
	atf_check_equal $(get_n_caches) 6

	# Limit the number of neighbor caches to 5
	atf_check -s exit:0 -o ignore rump.sysctl -w \
	    net.inet6.ip6.neighborgcthresh=5

	# ping to additional 4 destinations
	for i in $(seq 6 9); do
		atf_check -s exit:0 -o ignore rump.ping6 -n -X $TIMEOUT -c 1 \
		    ${IP6DST}$i
	done

	# More than 5 caches should be created in total, but exceeded caches
	# should be GC-ed
	if [ "$(get_n_caches)" -gt 5 ]; then
		atf_fail "Neighbor caches are not GC-ed"
	fi

	rump_server_destroy_ifaces
}

make_pkt_str_na()
{
	local ip=$1
	local mac=$2
	local pkt=
	pkt="$mac > 33:33:00:00:00:01, ethertype IPv6 (0x86dd), length 86:"
	pkt="$pkt $ip > ff02::1: ICMP6, neighbor advertisement"
	echo $pkt
}

ndp_link_activation_body()
{
	local linklocal=

	rump_server_start $SOCKSRC netinet6
	rump_server_start $SOCKDST netinet6

	setup_dst_server
	setup_src_server

	# flush old packets
	extract_new_packets bus1 > ./out

	export RUMP_SERVER=$SOCKSRC

	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 link \
	    b2:a1:00:00:00:01

	atf_check -s exit:0 sleep 1
	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out

	linklocal=$(rump.ifconfig shmif0 |awk '/fe80/ {print $2;}' |awk -F % '{print $1;}')
	$DEBUG && echo $linklocal

	pkt=$(make_pkt_str_na $linklocal b2:a1:00:00:00:01)
	atf_check -s not-exit:0 -x "cat ./out |grep -q '$pkt'"

	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 link \
	    b2:a1:00:00:00:02 active

	atf_check -s exit:0 sleep 1
	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out

	linklocal=$(rump.ifconfig shmif0 |awk '/fe80/ {print $2;}' |awk -F % '{print $1;}')
	$DEBUG && echo $linklocal

	pkt=$(make_pkt_str_na $linklocal b2:a1:00:00:00:02)
	atf_check -s exit:0 -x "cat ./out |grep -q '$pkt'"

	rump_server_destroy_ifaces
}

ndp_cache_expiration_cleanup()
{
	$DEBUG && dump
	cleanup
}

ndp_commands_cleanup()
{
	$DEBUG && dump
	cleanup
}

ndp_cache_overwriting_cleanup()
{
	$DEBUG && dump
	cleanup
}

ndp_neighborgcthresh_cleanup()
{
	$DEBUG && dump
	cleanup
}

ndp_link_activation_cleanup()
{
	$DEBUG && dump
	cleanup
}

atf_init_test_cases()
{
	atf_add_test_case ndp_cache_expiration
	atf_add_test_case ndp_commands
	atf_add_test_case ndp_cache_overwriting
	atf_add_test_case ndp_neighborgcthresh
	atf_add_test_case ndp_link_activation
}
