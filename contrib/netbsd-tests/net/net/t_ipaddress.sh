#	$NetBSD: t_ipaddress.sh,v 1.9 2016/12/15 02:43:56 ozaki-r Exp $
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
#

SOCK_LOCAL=unix://commsock1
BUS=bus

DEBUG=${DEBUG:-false}

test_same_address()
{
	local ip=10.0.0.1
	local net=10.0.0/24

	rump_server_start $SOCK_LOCAL
	rump_server_add_iface $SOCK_LOCAL shmif0 $BUS

	export RUMP_SERVER=$SOCK_LOCAL

	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 $ip/24
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 up
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10

	$DEBUG && rump.netstat -nr -f inet

	check_route $ip 'link#2' UHl lo0
	check_route $net 'link#2' UC shmif0

	# Delete the address
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 $ip delete

	$DEBUG && rump.netstat -nr -f inet

	check_route_no_entry $ip
	check_route_no_entry $net

	# Assign the same address again
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 $ip/24
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10

	$DEBUG && rump.netstat -nr -f inet

	check_route $ip 'link#2' UHl lo0
	check_route $net 'link#2' UC shmif0

	# Delete the address again
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 $ip delete

	$DEBUG && rump.netstat -nr -f inet

	check_route_no_entry $ip
	check_route_no_entry $net

	rump_server_destroy_ifaces
}

test_same_address6()
{
	local ip=fc00::1
	local net=fc00::/64

	rump_server_start $SOCK_LOCAL netinet6
	rump_server_add_iface $SOCK_LOCAL shmif0 $BUS

	export RUMP_SERVER=$SOCK_LOCAL

	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 inet6 $ip
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 up
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10

	$DEBUG && rump.netstat -nr -f inet6

	check_route $ip 'link#2' UHl lo0
	check_route $net 'link#2' UC shmif0

	# Delete the address
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 inet6 $ip delete

	$DEBUG && rump.netstat -nr -f inet6

	check_route_no_entry $ip
	check_route_no_entry $net

	# Assign the same address again
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 inet6 $ip
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10

	$DEBUG && rump.netstat -nr -f inet6

	check_route $ip 'link#2' UHl lo0
	check_route $net 'link#2' UC shmif0

	# Delete the address again
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 inet6 $ip delete

	$DEBUG && rump.netstat -nr -f inet6

	check_route_no_entry $ip
	check_route_no_entry $net

	rump_server_destroy_ifaces
}

test_auto_linklocal()
{

	rump_server_start $SOCK_LOCAL netinet6
	rump_server_add_iface $SOCK_LOCAL shmif0 $BUS

	export RUMP_SERVER=$SOCK_LOCAL

	#
	# Test enabled auto linklocal
	#

	# Check default value
	atf_check -s exit:0 -o match:"1" rump.sysctl -n net.inet6.ip6.auto_linklocal

	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 up
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10

	$DEBUG && rump.netstat -nr -f inet

	# IPv6 link-local address is set
	atf_check -s exit:0 -o match:"inet6 fe80::" rump.ifconfig shmif0

	#
	# Test disabled auto linklocal
	#
	atf_check -s exit:0 -o ignore rump.sysctl -w -q net.inet6.ip6.auto_linklocal=0

	rump_server_add_iface $SOCK_LOCAL shmif1 $BUS
	atf_check -s exit:0 -o ignore rump.ifconfig shmif1 up
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10

	$DEBUG && rump.netstat -nr -f inet

	# IPv6 link-local address is not set
	atf_check -s exit:0 -o not-match:"inet6 fe80::" rump.ifconfig shmif1

	rump_server_destroy_ifaces
}

add_test()
{
	local name=$1
	local desc="$2"

	atf_test_case "ipaddr_${name}" cleanup
	eval "ipaddr_${name}_head() { \
			atf_set \"descr\" \"${desc}\"; \
			atf_set \"require.progs\" \"rump_server\"; \
		}; \
	    ipaddr_${name}_body() { \
			test_${name}; \
		}; \
	    ipaddr_${name}_cleanup() { \
			$DEBUG && dump; \
			cleanup; \
		}"
	atf_add_test_case "ipaddr_${name}"
}

atf_init_test_cases()
{

	add_test same_address	"Assigning/deleting an IP address twice"
	add_test same_address6	"Assigning/deleting an IPv6 address twice"
	add_test auto_linklocal	"Assigning an IPv6 link-local address automatically"
}
