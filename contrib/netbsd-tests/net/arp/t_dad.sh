#	$NetBSD: t_dad.sh,v 1.13 2016/11/25 08:51:16 ozaki-r Exp $
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

atf_test_case dad_basic cleanup
atf_test_case dad_duplicated cleanup

dad_basic_head()
{
	atf_set "descr" "Tests for IPv4 DAD basic behavior"
	atf_set "require.progs" "rump_server"
}

dad_duplicated_head()
{
	atf_set "descr" "Tests for IPv4 DAD duplicated state"
	atf_set "require.progs" "rump_server"
}

setup_server()
{
	local sock=$1
	local ip=$2

	rump_server_add_iface $sock shmif0 bus1

	export RUMP_SERVER=$sock
	atf_check -s exit:0 rump.ifconfig shmif0 inet $ip/24
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig -w 10

	$DEBUG && rump.ifconfig shmif0
}

make_pkt_str()
{
	local target=$1
	local sender=$2
	pkt="> ff:ff:ff:ff:ff:ff, ethertype ARP (0x0806), length 42:"
	pkt="$pkt Request who-has $target tell $sender, length 28"
	echo $pkt
}

dad_basic_body()
{
	local pkt=

	rump_server_start $SOCKLOCAL
	rump_server_add_iface $SOCKLOCAL shmif0 bus1

	export RUMP_SERVER=$SOCKLOCAL

	atf_check -s exit:0 rump.ifconfig shmif0 inet 10.0.0.1/24
	atf_check -s exit:0 rump.ifconfig shmif0 inet 10.0.0.2/24 alias
	$DEBUG && rump.ifconfig shmif0

	atf_check -s exit:0 rump.ifconfig shmif0 up
	rump.ifconfig shmif0 > ./out
	$DEBUG && cat ./out

	# The primary address doesn't start with tentative state
	atf_check -s not-exit:0 -x "cat ./out |grep 10.0.0.1 |grep -iq tentative"
	# The alias address starts with tentative state
	# XXX we have no stable way to check this, so skip for now
	#atf_check -s exit:0 -x "cat ./out |grep 10.0.0.2 |grep -iq tentative"

	atf_check -s exit:0 sleep 2
	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out

	# Check DAD probe packets
	pkt=$(make_pkt_str 10.0.0.2 0.0.0.0)
	atf_check -s exit:0 -x "cat ./out |grep -q '$pkt'"
	# No DAD for the primary address
	pkt=$(make_pkt_str 10.0.0.1 0.0.0.0)
	atf_check -s not-exit:0 -x "cat ./out |grep -q '$pkt'"

	# Waiting for DAD complete
	atf_check -s exit:0 rump.ifconfig -w 10
	# Give a chance to send a DAD announce packet
	atf_check -s exit:0 sleep 1
	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out

	# Check the DAD announce packet
	pkt=$(make_pkt_str 10.0.0.2 10.0.0.2)
	atf_check -s exit:0 -x "cat ./out |grep -q '$pkt'"
	# The alias address left tentative
	atf_check -s not-exit:0 -x "rump.ifconfig shmif0 |grep 10.0.0.2 |grep -iq tentative"

	#
	# Add a new address on the fly
	#
	atf_check -s exit:0 rump.ifconfig shmif0 inet 10.0.0.3/24 alias

	# The new address starts with tentative state
	# XXX we have no stable way to check this, so skip for now
	#atf_check -s exit:0 -x "rump.ifconfig shmif0 |grep 10.0.0.3 |grep -iq tentative"

	# Check DAD probe packets
	atf_check -s exit:0 sleep 2
	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out
	pkt=$(make_pkt_str 10.0.0.3 0.0.0.0)
	atf_check -s exit:0 -x "cat ./out |grep -q '$pkt'"

	# Waiting for DAD complete
	atf_check -s exit:0 rump.ifconfig -w 10
	# Give a chance to send a DAD announce packet
	atf_check -s exit:0 sleep 1
	extract_new_packets bus1 > ./out
	$DEBUG && cat ./out

	# Check the DAD announce packet
	pkt=$(make_pkt_str 10.0.0.3 10.0.0.3)
	atf_check -s exit:0 -x "cat ./out |grep -q '$pkt'"
	# The new address left tentative
	atf_check -s not-exit:0 -x "rump.ifconfig shmif0 |grep 10.0.0.3 |grep -iq tentative"

	rump_server_destroy_ifaces
}

dad_duplicated_body()
{
	local localip1=10.0.1.1
	local localip2=10.0.1.11
	local peerip=10.0.1.2

	rump_server_start $SOCKLOCAL
	rump_server_start $SOCKPEER

	setup_server $SOCKLOCAL $localip1
	setup_server $SOCKPEER $peerip

	export RUMP_SERVER=$SOCKLOCAL

	# The primary address isn't marked as duplicated
	atf_check -s not-exit:0 -x "rump.ifconfig shmif0 |grep $localip1 |grep -iq duplicated"

	#
	# Add a new address duplicated with the peer server
	#
	atf_check -s exit:0 rump.ifconfig shmif0 inet $peerip alias
	atf_check -s exit:0 sleep 1

	# The new address is marked as duplicated
	atf_check -s exit:0 -x "rump.ifconfig shmif0 |grep $peerip |grep -iq duplicated"

	# A unique address isn't marked as duplicated
	atf_check -s exit:0 rump.ifconfig shmif0 inet $localip2 alias
	atf_check -s exit:0 sleep 1
	atf_check -s not-exit:0 -x "rump.ifconfig shmif0 |grep $localip2 |grep -iq duplicated"

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

atf_init_test_cases()
{
	atf_add_test_case dad_basic
	atf_add_test_case dad_duplicated
}
