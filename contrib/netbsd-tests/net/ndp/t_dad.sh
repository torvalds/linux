#	$NetBSD: t_dad.sh,v 1.12 2016/11/25 08:51:17 ozaki-r Exp $
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

SOCKLOCAL=unix://commsock1
SOCKPEER=unix://commsock2

DEBUG=${DEBUG:-false}

duplicated="[Dd][Uu][Pp][Ll][Ii][Cc][Aa][Tt][Ee][Dd]"

atf_test_case dad_basic cleanup
atf_test_case dad_duplicated cleanup
atf_test_case dad_count cleanup

dad_basic_head()
{
	atf_set "descr" "Tests for IPv6 DAD basic behavior"
	atf_set "require.progs" "rump_server"
}

dad_duplicated_head()
{
	atf_set "descr" "Tests for IPv6 DAD duplicated state"
	atf_set "require.progs" "rump_server"
}

dad_count_head()
{
	atf_set "descr" "Tests for IPv6 DAD count behavior"
	atf_set "require.progs" "rump_server"
}

setup_server()
{
	local sock=$1
	local ip=$2

	rump_server_add_iface $sock shmif0 bus1

	export RUMP_SERVER=$sock
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $ip
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig -w 10

	$DEBUG && rump.ifconfig shmif0
}

make_ns_pkt_str()
{
	local id=$1
	local target=$2
	pkt="33:33:ff:00:00:0${id}, ethertype IPv6 (0x86dd), length 78: ::"
	pkt="$pkt > ff02::1:ff00:${id}: ICMP6, neighbor solicitation,"
	pkt="$pkt who has $target, length 24"
	echo $pkt
}

dad_basic_body()
{
	local pkt=
	local localip1=fc00::1
	local localip2=fc00::2
	local localip3=fc00::3

	rump_server_start $SOCKLOCAL netinet6
	rump_server_add_iface $SOCKLOCAL shmif0 bus1

	export RUMP_SERVER=$SOCKLOCAL
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $localip1
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $localip2
	$DEBUG && rump.ifconfig shmif0

	atf_check -s exit:0 rump.ifconfig shmif0 up
	rump.ifconfig shmif0 > ./out
	$DEBUG && cat ./out

	# The primary address doesn't start with tentative state
	atf_check -s not-exit:0 -x "cat ./out |grep $localip1 |grep -q tentative"
	# The alias address starts with tentative state
	# XXX we have no stable way to check this, so skip for now
	#atf_check -s exit:0 -x "cat ./out |grep $localip2 |grep -q tentative"

	atf_check -s exit:0 sleep 2
	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out

	# Check DAD probe packets (Neighbor Solicitation Message)
	pkt=$(make_ns_pkt_str 2 $localip2)
	atf_check -s exit:0 -x "cat ./out |grep -q '$pkt'"
	# No DAD for the primary address
	pkt=$(make_ns_pkt_str 1 $localip1)
	atf_check -s not-exit:0 -x "cat ./out |grep -q '$pkt'"

	# Waiting for DAD complete
	atf_check -s exit:0 rump.ifconfig -w 10
	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out

	# IPv6 DAD doesn't announce (Neighbor Advertisement Message)

	# The alias address left tentative
	atf_check -s not-exit:0 -x "rump.ifconfig shmif0 |grep $localip2 |grep -q tentative"

	#
	# Add a new address on the fly
	#
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $localip3

	# The new address starts with tentative state
	# XXX we have no stable way to check this, so skip for now
	#atf_check -s exit:0 -x "rump.ifconfig shmif0 |grep $localip3 |grep -q tentative"

	# Check DAD probe packets (Neighbor Solicitation Message)
	atf_check -s exit:0 sleep 2
	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out
	pkt=$(make_ns_pkt_str 3 $localip3)
	atf_check -s exit:0 -x "cat ./out |grep -q '$pkt'"

	# Waiting for DAD complete
	atf_check -s exit:0 rump.ifconfig -w 10
	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out

	# IPv6 DAD doesn't announce (Neighbor Advertisement Message)

	# The new address left tentative
	atf_check -s not-exit:0 -x "rump.ifconfig shmif0 |grep $localip3 |grep -q tentative"

	rump_server_destroy_ifaces
}

dad_duplicated_body()
{
	local localip1=fc00::1
	local localip2=fc00::11
	local peerip=fc00::2

	rump_server_start $SOCKLOCAL netinet6
	rump_server_start $SOCKPEER netinet6

	setup_server $SOCKLOCAL $localip1
	setup_server $SOCKPEER $peerip

	export RUMP_SERVER=$SOCKLOCAL

	# The primary address isn't marked as duplicated
	atf_check -s exit:0 -o not-match:"$localip1.+$duplicated" \
	    rump.ifconfig shmif0

	#
	# Add a new address duplicated with the peer server
	#
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $peerip
	atf_check -s exit:0 sleep 1

	# The new address is marked as duplicated
	atf_check -s exit:0 -o match:"$peerip.+$duplicated" \
	    rump.ifconfig shmif0

	# A unique address isn't marked as duplicated
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $localip2
	atf_check -s exit:0 sleep 1
	atf_check -s exit:0 -o not-match:"$localip2.+$duplicated" \
	    rump.ifconfig shmif0

	rump_server_destroy_ifaces
}

dad_count_test()
{
	local pkt=
	local count=$1
	local id=$2
	local target=$3

	#
	# Set DAD count to $count
	#
	atf_check -s exit:0 rump.sysctl -w -q net.inet6.ip6.dad_count=$count

	# Add a new address
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $target

	# Waiting for DAD complete
	atf_check -s exit:0 rump.ifconfig -w 20

	# Check the number of DAD probe packets (Neighbor Solicitation Message)
	atf_check -s exit:0 sleep 2
	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out
	pkt=$(make_ns_pkt_str $id $target)
	atf_check -s exit:0 -o match:"$count" \
	    -x "cat ./out |grep '$pkt' | wc -l | tr -d ' '"
}

dad_count_body()
{
	local localip1=fc00::1
	local localip2=fc00::2

	rump_server_start $SOCKLOCAL netinet6
	rump_server_add_iface $SOCKLOCAL shmif0 bus1

	export RUMP_SERVER=$SOCKLOCAL

	# Check default value
	atf_check -s exit:0 -o match:"1" rump.sysctl -n net.inet6.ip6.dad_count

	# Setup interface
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 sleep 2
	rump.ifconfig shmif0 > ./out
	$DEBUG && cat ./out

	#
	# Set and test DAD count (count=1)
	#
	dad_count_test 1 1 $localip1

	#
	# Set and test DAD count (count=8)
	#
	dad_count_test 8 2 $localip2

	rump_server_destroy_ifaces
}

dad_basic_cleanup()
{
	$DEBUG && dump
	cleanup
}

dad_duplicated_cleanup()
{
	$DEBUG && dump
	cleanup
}

dad_count_cleanup()
{
	$DEBUG && dump
	cleanup
}

atf_init_test_cases()
{
	atf_add_test_case dad_basic
	atf_add_test_case dad_duplicated
	atf_add_test_case dad_count
}
