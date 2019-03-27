#	$NetBSD: t_forwarding.sh,v 1.19 2016/11/25 08:51:17 ozaki-r Exp $
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
SOCKFWD=unix://commsock2
SOCKDST=unix://commsock3
IP4SRC=10.0.1.2
IP4SRCGW=10.0.1.1
IP4DSTGW=10.0.2.1
IP4DST=10.0.2.2
IP4DST_BCAST=10.0.2.255
IP6SRC=fc00:0:0:1::2
IP6SRCGW=fc00:0:0:1::1
IP6DSTGW=fc00:0:0:2::1
IP6DST=fc00:0:0:2::2
HTML_FILE=index.html

DEBUG=${DEBUG:-false}
TIMEOUT=5

atf_test_case ipforwarding_v4 cleanup
atf_test_case ipforwarding_v6 cleanup
atf_test_case ipforwarding_fastforward_v4 cleanup
atf_test_case ipforwarding_fastforward_v6 cleanup
atf_test_case ipforwarding_misc cleanup

ipforwarding_v4_head()
{
	atf_set "descr" "Does IPv4 forwarding tests"
	atf_set "require.progs" "rump_server"
}

ipforwarding_v6_head()
{
	atf_set "descr" "Does IPv6 forwarding tests"
	atf_set "require.progs" "rump_server"
}

ipforwarding_fastforward_v4_head()
{
	atf_set "descr" "Tests for IPv4 fastforward"
	atf_set "require.progs" "rump_server"
}

ipforwarding_fastforward_v6_head()
{
	atf_set "descr" "Tests for IPv6 fastfoward"
	atf_set "require.progs" "rump_server"
}

ipforwarding_misc_head()
{
	atf_set "descr" "Does IPv4 forwarding tests"
	atf_set "require.progs" "rump_server"
}

setup_endpoint()
{
	sock=${1}
	addr=${2}
	bus=${3}
	mode=${4}
	gw=${5}

	rump_server_add_iface $sock shmif0 $bus

	export RUMP_SERVER=${sock}
	if [ $mode = "ipv6" ]; then
		atf_check -s exit:0 rump.ifconfig shmif0 inet6 ${addr}
		atf_check -s exit:0 -o ignore rump.route add -inet6 default ${gw}
	else
		atf_check -s exit:0 rump.ifconfig shmif0 inet ${addr} netmask 0xffffff00
		atf_check -s exit:0 -o ignore rump.route add default ${gw}
	fi
	atf_check -s exit:0 rump.ifconfig shmif0 up

	if $DEBUG; then
		rump.ifconfig shmif0
		rump.netstat -nr
	fi	
}

test_endpoint()
{
	sock=${1}
	addr=${2}
	bus=${3}
	mode=${4}

	export RUMP_SERVER=${sock}
	atf_check -s exit:0 -o match:shmif0 rump.ifconfig
	if [ $mode = "ipv6" ]; then
		atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT ${addr}
	else
		atf_check -s exit:0 -o ignore rump.ping -n -w $TIMEOUT -c 1 ${addr}
	fi
}

setup_forwarder()
{
	mode=${1}

	rump_server_add_iface $SOCKFWD shmif0 bus1
	rump_server_add_iface $SOCKFWD shmif1 bus2

	export RUMP_SERVER=$SOCKFWD

	if [ $mode = "ipv6" ]; then
		atf_check -s exit:0 rump.ifconfig shmif0 inet6 ${IP6SRCGW}
		atf_check -s exit:0 rump.ifconfig shmif1 inet6 ${IP6DSTGW}
	else
		atf_check -s exit:0 rump.ifconfig shmif0 inet ${IP4SRCGW} netmask 0xffffff00
		atf_check -s exit:0 rump.ifconfig shmif1 inet ${IP4DSTGW} netmask 0xffffff00
	fi

	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig shmif1 up

	if $DEBUG; then
		rump.netstat -nr
		if [ $mode = "ipv6" ]; then
			rump.sysctl net.inet6.ip6.forwarding
		else
			rump.sysctl net.inet.ip.forwarding
		fi
	fi
}

setup()
{
	rump_server_start $SOCKSRC
	rump_server_start $SOCKFWD
	rump_server_start $SOCKDST

	setup_endpoint $SOCKSRC $IP4SRC bus1 ipv4 $IP4SRCGW
	setup_endpoint $SOCKDST $IP4DST bus2 ipv4 $IP4DSTGW
	setup_forwarder ipv4
}

setup6()
{
	rump_server_start $SOCKSRC netinet6
	rump_server_start $SOCKFWD netinet6
	rump_server_start $SOCKDST netinet6

	setup_endpoint $SOCKSRC $IP6SRC bus1 ipv6 $IP6SRCGW
	setup_endpoint $SOCKDST $IP6DST bus2 ipv6 $IP6DSTGW
	setup_forwarder ipv6
}

test_http_get()
{
	local ip=$1

	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 rump.arp -d -a

	export RUMP_SERVER=$SOCKSRC

	# get the webpage
	atf_check -s exit:0 env LD_PRELOAD=/usr/lib/librumphijack.so 	\
	    ftp -q $TIMEOUT -o out http://$ip/$HTML_FILE
}

test_setup()
{
	test_endpoint $SOCKSRC $IP4SRC bus1 ipv4
	test_endpoint $SOCKDST $IP4DST bus2 ipv4

	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 -o match:shmif0 rump.ifconfig
	atf_check -s exit:0 -o match:shmif1 rump.ifconfig

	atf_check -s exit:0 -o ignore rump.ping -n -w $TIMEOUT -c 1 ${IP4SRCGW}
	atf_check -s exit:0 -o ignore rump.ping -n -w $TIMEOUT -c 1 ${IP4DSTGW}
}

test_setup6()
{
	test_endpoint $SOCKSRC $IP6SRC bus1 ipv6
	test_endpoint $SOCKDST $IP6DST bus2 ipv6

	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 -o match:shmif0 rump.ifconfig
	atf_check -s exit:0 -o match:shmif1 rump.ifconfig

	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT ${IP6SRCGW}
	atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT ${IP6DSTGW}
}

setup_forwarding()
{
	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 -o ignore rump.sysctl -w net.inet.ip.forwarding=1
}

setup_forwarding6()
{
	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 -o ignore rump.sysctl -w net.inet6.ip6.forwarding=1
}

setup_directed_broadcast()
{
	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 -o ignore rump.sysctl -w net.inet.ip.directed-broadcast=1
}

setup_icmp_bmcastecho()
{
	export RUMP_SERVER=$SOCKDST
	atf_check -s exit:0 -o ignore rump.sysctl -w net.inet.icmp.bmcastecho=1
}

teardown_forwarding()
{
	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 -o ignore rump.sysctl -w net.inet.ip.forwarding=0
}

teardown_forwarding6()
{
	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 -o ignore rump.sysctl -w net.inet6.ip6.forwarding=0
}

teardown_directed_broadcast()
{
	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 -o ignore rump.sysctl -w net.inet.ip.directed-broadcast=0
}

teardown_icmp_bmcastecho()
{
	export RUMP_SERVER=$SOCKDST
	atf_check -s exit:0 -o ignore rump.sysctl -w net.inet.icmp.bmcastecho=0
}

teardown_interfaces()
{

	rump_server_destroy_ifaces
}

test_setup_forwarding()
{
	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 -o match:"net.inet.ip.forwarding = 1" \
	    rump.sysctl net.inet.ip.forwarding
}
test_setup_forwarding6()
{
	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 -o match:"net.inet6.ip6.forwarding = 1" \
	    rump.sysctl net.inet6.ip6.forwarding
}

test_teardown_forwarding()
{
	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 -o match:"net.inet.ip.forwarding = 0" \
	    rump.sysctl net.inet.ip.forwarding
}
test_teardown_forwarding6()
{
	export RUMP_SERVER=$SOCKFWD
	atf_check -s exit:0 -o match:"net.inet6.ip6.forwarding = 0" \
	    rump.sysctl net.inet6.ip6.forwarding
}

test_ping_failure()
{
	export RUMP_SERVER=$SOCKSRC
	atf_check -s not-exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IP4DST
	export RUMP_SERVER=$SOCKDST
	atf_check -s not-exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IP4SRC
}

test_ping_success()
{
	export RUMP_SERVER=$SOCKSRC
	$DEBUG && rump.ifconfig -v shmif0
	atf_check -s exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IP4SRCGW
	atf_check -s exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IP4DST
	$DEBUG && rump.ifconfig -v shmif0

	export RUMP_SERVER=$SOCKDST
	$DEBUG && rump.ifconfig -v shmif0
	atf_check -s exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IP4DSTGW
	atf_check -s exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IP4SRC
	$DEBUG && rump.ifconfig -v shmif0
}

test_ping_ttl()
{
	export RUMP_SERVER=$SOCKSRC
	$DEBUG && rump.ifconfig -v shmif0
	atf_check -s exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 -T 1 $IP4SRCGW
	atf_check -s not-exit:0 -o match:'Time To Live exceeded' \
	    rump.ping -v -n -w $TIMEOUT -c 1 -T 1 $IP4DST
	atf_check -s exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 -T 2 $IP4DST
	$DEBUG && rump.ifconfig -v shmif0
}

test_sysctl_ttl()
{
	local ip=$1

	export RUMP_SERVER=$SOCKSRC
	$DEBUG && rump.ifconfig -v shmif0

	atf_check -s exit:0 -o ignore rump.sysctl -w net.inet.ip.ttl=1
	# get the webpage
	atf_check -s not-exit:0 -e match:'timed out' \
		env LD_PRELOAD=/usr/lib/librumphijack.so	\
		ftp -q $TIMEOUT -o out http://$ip/$HTML_FILE


	atf_check -s exit:0 -o ignore rump.sysctl -w net.inet.ip.ttl=2
	# get the webpage
	atf_check -s exit:0 env LD_PRELOAD=/usr/lib/librumphijack.so 	\
		ftp -q $TIMEOUT -o out http://$ip/$HTML_FILE

	atf_check -s exit:0 -o ignore rump.sysctl -w net.inet.ip.ttl=64
	$DEBUG && rump.ifconfig -v shmif0
}

test_directed_broadcast()
{
	setup_icmp_bmcastecho

	setup_directed_broadcast
	export RUMP_SERVER=$SOCKSRC
	atf_check -s exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IP4DST_BCAST

	teardown_directed_broadcast
	export RUMP_SERVER=$SOCKSRC
	atf_check -s not-exit:0 -o ignore rump.ping -q -n -w $TIMEOUT -c 1 $IP4DST_BCAST

	teardown_icmp_bmcastecho
}

test_ping6_failure()
{
	export RUMP_SERVER=$SOCKSRC
	atf_check -s not-exit:0 -o ignore rump.ping6 -q -n -c 1 -X $TIMEOUT $IP6DST
	export RUMP_SERVER=$SOCKDST
	atf_check -s not-exit:0 -o ignore rump.ping6 -q -n -c 1 -X $TIMEOUT $IP6SRC
}

test_ping6_success()
{
	export RUMP_SERVER=$SOCKSRC
	$DEBUG && rump.ifconfig -v shmif0
	atf_check -s exit:0 -o ignore rump.ping6 -q -n -c 1 -X $TIMEOUT $IP6SRCGW
	atf_check -s exit:0 -o ignore rump.ping6 -q -n -c 1 -X $TIMEOUT $IP6DST
	$DEBUG && rump.ifconfig -v shmif0

	export RUMP_SERVER=$SOCKDST
	$DEBUG && rump.ifconfig -v shmif0
	atf_check -s exit:0 -o ignore rump.ping6 -q -n -c 1 -X $TIMEOUT $IP6DSTGW
	atf_check -s exit:0 -o ignore rump.ping6 -q -n -c 1 -X $TIMEOUT $IP6SRC
	$DEBUG && rump.ifconfig -v shmif0
}

test_hoplimit()
{
	export RUMP_SERVER=$SOCKSRC
	$DEBUG && rump.ifconfig -v shmif0
	atf_check -s exit:0 -o ignore rump.ping6 -q -n -c 1 -h 1 -X $TIMEOUT $IP6SRCGW
	atf_check -s not-exit:0 -o match:'Time to live exceeded' \
	    rump.ping6 -v -n -c 1 -h 1 -X $TIMEOUT $IP6DST
	atf_check -s exit:0 -o ignore rump.ping6 -q -n -c 1 -h 2 -X $TIMEOUT $IP6DST
	$DEBUG && rump.ifconfig -v shmif0
}

ipforwarding_v4_body()
{
	setup
	test_setup

	setup_forwarding
	test_setup_forwarding
	test_ping_success

	teardown_forwarding
	test_teardown_forwarding
	test_ping_failure

	teardown_interfaces
}

ipforwarding_v6_body()
{
	setup6
	test_setup6

	setup_forwarding6
	test_setup_forwarding6
	test_ping6_success
	test_hoplimit

	teardown_forwarding6
	test_teardown_forwarding6
	test_ping6_failure

	teardown_interfaces
}

ipforwarding_fastforward_v4_body()
{
	setup
	test_setup

	setup_forwarding
	test_setup_forwarding

	touch $HTML_FILE
	start_httpd $SOCKDST $IP4DST
	$DEBUG && rump.netstat -a

	test_http_get $IP4DST

	teardown_interfaces
}

ipforwarding_fastforward_v6_body()
{
	setup6
	test_setup6

	setup_forwarding6
	test_setup_forwarding6

	touch $HTML_FILE
	start_httpd $SOCKDST $IP6DST
	$DEBUG && rump.netstat -a

	test_http_get "[$IP6DST]"

	teardown_interfaces
}

ipforwarding_misc_body()
{
	setup
	test_setup

	setup_forwarding
	test_setup_forwarding

	test_ping_ttl

	test_directed_broadcast

	touch $HTML_FILE
	start_httpd $SOCKDST $IP4DST
	$DEBUG && rump.netstat -a

	test_sysctl_ttl $IP4DST

	teardown_interfaces
	return 0
}

ipforwarding_v4_cleanup()
{
	$DEBUG && dump
	cleanup
}

ipforwarding_v6_cleanup()
{
	$DEBUG && dump
	cleanup
}

ipforwarding_fastforward_v4_cleanup()
{
	$DEBUG && dump
	stop_httpd
	cleanup
}

ipforwarding_fastforward_v6_cleanup()
{
	$DEBUG && dump
	stop_httpd
	cleanup
}

ipforwarding_misc_cleanup()
{
	$DEBUG && dump
	stop_httpd
	cleanup
}

atf_init_test_cases()
{
	atf_add_test_case ipforwarding_v4
	atf_add_test_case ipforwarding_v6
	atf_add_test_case ipforwarding_fastforward_v4
	atf_add_test_case ipforwarding_fastforward_v6
	atf_add_test_case ipforwarding_misc
}
