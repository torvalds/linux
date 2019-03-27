#	$NetBSD: t_mtudisc.sh,v 1.8 2016/12/21 01:16:18 ozaki-r Exp $
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

SOCKLOCAL=unix://commsock1
SOCKGATEWAY=unix://commsock2
SOCKREMOTE=unix://commsock3
HTML_FILE=index.html

DEBUG=${DEBUG:-false}

atf_test_case mtudisc_basic cleanup

mtudisc_basic_head()
{
	atf_set "descr" "Tests for IPv4 Path MTU Dicorvery basic behavior"
	atf_set "require.progs" "rump_server"
}

setup_server()
{
	local sock=$1
	local if=$2
	local bus=$3
	local ip=$4

	rump_server_add_iface $sock $if $bus

	export RUMP_SERVER=$sock
	atf_check -s exit:0 rump.ifconfig $if $ip
	atf_check -s exit:0 rump.ifconfig $if up
	atf_check -s exit:0 rump.ifconfig -w 10

	$DEBUG && rump.ifconfig $if
}

prepare_download_file()
{
	local file=$1
	local data="0123456789"

	touch $file
	for i in `seq 1 512`
	do
		echo $data >> $file
	done
}

do_http_get()
{
	local ip=$1
	local ret=$2
	local timeout=5

	# get the webpage
	atf_check -s exit:$ret env LD_PRELOAD=/usr/lib/librumphijack.so	\
	    ftp -q $timeout -o ./out http://$ip/$HTML_FILE
}

mtudisc_basic_body()
{
	local pkt=
	local local_ip=10.0.0.2
	local gateway_local_ip=10.0.0.1
	local gateway_remote_ip=10.0.1.1
	local remote_ip=10.0.1.2
	local prefixlen=24

	rump_server_start $SOCKLOCAL
	rump_server_start $SOCKGATEWAY
	rump_server_start $SOCKREMOTE

	#
	# Setup servers
	#
	# [local server]                 [gateway server]          [remote server with httpd]
	#       | 10.0.0.2        10.0.0.1 |          | 10.0.1.1       10.0.1.2 |
	# shmif0(mtu=1500) ----- shmif1(mtu=1280)   shmif0(mtu=1500) ----- shmif0(mtu=1500)
	#

	# Assign IP addresses
	setup_server $SOCKLOCAL shmif0 bus1 $local_ip/$prefixlen
	setup_server $SOCKGATEWAY shmif0 bus1 $gateway_local_ip/$prefixlen
	setup_server $SOCKGATEWAY shmif1 bus2 $gateway_remote_ip/$prefixlen
	setup_server $SOCKREMOTE shmif0 bus2 $remote_ip/$prefixlen

	### Setup gateway server
	export RUMP_SERVER=$SOCKGATEWAY

	# Set mtu of shmif0 to 1280
	export RUMP_SERVER=$SOCKGATEWAY
	atf_check -s exit:0 rump.ifconfig shmif0 mtu 1280

	# Enable IPv4 forwarding
	atf_check -s exit:0 rump.sysctl -w -q net.inet.ip.forwarding=1

	### Setup remote server
	export RUMP_SERVER=$SOCKREMOTE

	# Check default value
	atf_check -s exit:0 -o match:"1" rump.sysctl -n net.inet.ip.mtudisc

	# Start httpd daemon
	prepare_download_file $HTML_FILE
	start_httpd $SOCKREMOTE $remote_ip
	$DEBUG && rump.netstat -a -f inet

	# Teach the peer thar 10.0.0.2(local serer) is behind 10.0.1.1(gateway server)
	atf_check -s exit:0 -o ignore rump.route add $local_ip/32 $gateway_remote_ip

	### Setup local server
	export RUMP_SERVER=$SOCKLOCAL

	# Teach the peer thar 10.0.1.2(remote serer) is behind 10.0.0.1(gateway server)
	atf_check -s exit:0 -o ignore rump.route add $remote_ip/32 $gateway_local_ip

	# Don't accept fragmented packets
	atf_check -s exit:0 -o ignore rump.sysctl -w -q net.inet.ip.maxfragpackets=0

	#
	# Test disabled path mtu discorvery
	#
	export RUMP_SERVER=$SOCKREMOTE
	atf_check -s exit:0 -o ignore rump.sysctl -w -q net.inet.ip.mtudisc=0

	# Get the webpage (expect: failed)
	export RUMP_SERVER=$SOCKLOCAL
	do_http_get $remote_ip 1
	$DEBUG && extract_new_packets bus2 > ./out
	$DEBUG && cat ./out

	# Check path mtu size on remote server
	export RUMP_SERVER=$SOCKREMOTE
	atf_check -s exit:0 \
		-o match:"^10.0.0.2 +10.0.1.1 +UGHS +- +- +- +shmif0" \
		rump.netstat -nr -f inet

	#
	# Test enabled path mtu discorvery
	#
	export RUMP_SERVER=$SOCKREMOTE
	atf_check -s exit:0 -o ignore rump.sysctl -w -q net.inet.ip.mtudisc=1

	# Get the webpage (expect: success)
	export RUMP_SERVER=$SOCKLOCAL
	do_http_get $remote_ip 0
	$DEBUG && extract_new_packets bus2 > ./out
	$DEBUG && cat ./out

	# Check path mtu size on remote server
	export RUMP_SERVER=$SOCKREMOTE
	atf_check -s exit:0 \
		-o match:"^10.0.0.2 +10.0.1.1 +UGHS +- +- +1280 +shmif0" \
		rump.netstat -nr -f inet

	rump_server_destroy_ifaces
}

mtudisc_basic_cleanup()
{
	$DEBUG && dump
	stop_httpd
	cleanup
}

atf_init_test_cases()
{
	atf_add_test_case mtudisc_basic
}
