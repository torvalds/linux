#	$NetBSD: t_gif.sh,v 1.9 2016/12/21 09:46:39 ozaki-r Exp $
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

SOCK1=unix://commsock1 # for ROUTER1
SOCK2=unix://commsock2 # for ROUTER2
ROUTER1_LANIP=192.168.1.1
ROUTER1_LANNET=192.168.1.0/24
ROUTER1_WANIP=10.0.0.1
ROUTER1_GIFIP=172.16.1.1
ROUTER1_WANIP_DUMMY=10.0.0.11
ROUTER1_GIFIP_DUMMY=172.16.11.1
ROUTER1_GIFIP_RECURSIVE1=172.16.101.1
ROUTER1_GIFIP_RECURSIVE2=172.16.201.1
ROUTER2_LANIP=192.168.2.1
ROUTER2_LANNET=192.168.2.0/24
ROUTER2_WANIP=10.0.0.2
ROUTER2_GIFIP=172.16.2.1
ROUTER2_WANIP_DUMMY=10.0.0.12
ROUTER2_GIFIP_DUMMY=172.16.12.1
ROUTER2_GIFIP_RECURSIVE1=172.16.102.1
ROUTER2_GIFIP_RECURSIVE2=172.16.202.1

ROUTER1_LANIP6=fc00:1::1
ROUTER1_LANNET6=fc00:1::/64
ROUTER1_WANIP6=fc00::1
ROUTER1_GIFIP6=fc00:3::1
ROUTER1_WANIP6_DUMMY=fc00::11
ROUTER1_GIFIP6_DUMMY=fc00:13::1
ROUTER1_GIFIP6_RECURSIVE1=fc00:103::1
ROUTER1_GIFIP6_RECURSIVE2=fc00:203::1
ROUTER2_LANIP6=fc00:2::1
ROUTER2_LANNET6=fc00:2::/64
ROUTER2_WANIP6=fc00::2
ROUTER2_GIFIP6=fc00:4::1
ROUTER2_WANIP6_DUMMY=fc00::12
ROUTER2_GIFIP6_DUMMY=fc00:14::1
ROUTER2_GIFIP6_RECURSIVE1=fc00:104::1
ROUTER2_GIFIP6_RECURSIVE2=fc00:204::1

DEBUG=${DEBUG:-true}
TIMEOUT=5

setup_router()
{
	sock=${1}
	lan=${2}
	lan_mode=${3}
	wan=${4}
	wan_mode=${5}

	rump_server_add_iface $sock shmif0 bus0
	rump_server_add_iface $sock shmif1 bus1

	export RUMP_SERVER=${sock}
	if [ ${lan_mode} = "ipv6" ]; then
		atf_check -s exit:0 rump.ifconfig shmif0 inet6 ${lan}
	else
		atf_check -s exit:0 rump.ifconfig shmif0 inet ${lan} netmask 0xffffff00
	fi
	atf_check -s exit:0 rump.ifconfig shmif0 up
	rump.ifconfig shmif0

	if [ ${wan_mode} = "ipv6" ]; then
		atf_check -s exit:0 rump.ifconfig shmif1 inet6 ${wan}
	else
		atf_check -s exit:0 rump.ifconfig shmif1 inet ${wan} netmask 0xff000000
	fi
	atf_check -s exit:0 rump.ifconfig shmif1 up
	rump.ifconfig shmif1
}

test_router()
{
	sock=${1}
	lan=${2}
	lan_mode=${3}
	wan=${4}
	wan_mode=${5}

	export RUMP_SERVER=${sock}
	atf_check -s exit:0 -o match:shmif0 rump.ifconfig
	if [ ${lan_mode} = "ipv6" ]; then
		atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT ${lan}
	else
		atf_check -s exit:0 -o ignore rump.ping -n -c 1 -w $TIMEOUT ${lan}
	fi

	atf_check -s exit:0 -o match:shmif1 rump.ifconfig
	if [ ${wan_mode} = "ipv6" ]; then
		atf_check -s exit:0 -o ignore rump.ping6 -n -c 1 -X $TIMEOUT ${wan}
	else
		atf_check -s exit:0 -o ignore rump.ping -n -c 1 -w $TIMEOUT ${wan}
	fi
}

setup()
{
	inner=${1}
	outer=${2}

	rump_server_start $SOCK1 netinet6 gif
	rump_server_start $SOCK2 netinet6 gif

	router1_lan=""
	router1_lan_mode=""
	router2_lan=""
	router2_lan_mode=""
	if [ ${inner} = "ipv6" ]; then
		router1_lan=$ROUTER1_LANIP6
		router1_lan_mode="ipv6"
		router2_lan=$ROUTER2_LANIP6
		router2_lan_mode="ipv6"
	else
		router1_lan=$ROUTER1_LANIP
		router1_lan_mode="ipv4"
		router2_lan=$ROUTER2_LANIP
		router2_lan_mode="ipv4"
	fi

	if [ ${outer} = "ipv6" ]; then
		setup_router $SOCK1 ${router1_lan} ${router1_lan_mode} \
			$ROUTER1_WANIP6 ipv6
		setup_router $SOCK2 ${router2_lan} ${router2_lan_mode} \
			$ROUTER2_WANIP6 ipv6
	else
		setup_router $SOCK1 ${router1_lan} ${router1_lan_mode} \
			$ROUTER1_WANIP ipv4
		setup_router $SOCK2 ${router2_lan} ${router2_lan_mode} \
			$ROUTER2_WANIP ipv4
	fi
}

test_setup()
{
	inner=${1}
	outer=${2}

	router1_lan=""
	router1_lan_mode=""
	router2_lan=""
	router2_lan_mode=""
	if [ ${inner} = "ipv6" ]; then
		router1_lan=$ROUTER1_LANIP6
		router1_lan_mode="ipv6"
		router2_lan=$ROUTER2_LANIP6
		router2_lan_mode="ipv6"
	else
		router1_lan=$ROUTER1_LANIP
		router1_lan_mode="ipv4"
		router2_lan=$ROUTER2_LANIP
		router2_lan_mode="ipv4"
	fi
	if [ ${outer} = "ipv6" ]; then
		test_router $SOCK1 ${router1_lan} ${router1_lan_mode} \
			$ROUTER1_WANIP6 ipv6
		test_router $SOCK2 ${router2_lan} ${router2_lan_mode} \
			$ROUTER2_WANIP6 ipv6
	else
		test_router $SOCK1 ${router1_lan} ${router1_lan_mode} \
			$ROUTER1_WANIP ipv4
		test_router $SOCK2 ${router2_lan} ${router2_lan_mode} \
			$ROUTER2_WANIP ipv4
	fi
}

setup_if_gif()
{
	sock=${1}
	addr=${2}
	remote=${3}
	inner=${4}
	src=${5}
	dst=${6}
	peernet=${7}

	export RUMP_SERVER=${sock}
	atf_check -s exit:0 rump.ifconfig gif0 create
	atf_check -s exit:0 rump.ifconfig gif0 tunnel ${src} ${dst}
	if [ ${inner} = "ipv6" ]; then
		atf_check -s exit:0 rump.ifconfig gif0 inet6 ${addr}/128 ${remote}
		atf_check -s exit:0 -o ignore rump.route add -inet6 ${peernet} ${addr}
	else
		atf_check -s exit:0 rump.ifconfig gif0 inet ${addr}/32 ${remote}
		atf_check -s exit:0 -o ignore rump.route add -inet ${peernet} ${addr}
	fi

	rump.ifconfig gif0
	rump.route -nL show
}

setup_tunnel()
{
	inner=${1}
	outer=${2}

	addr=""
	remote=""
	src=""
	dst=""
	peernet=""

	if [ ${inner} = "ipv6" ]; then
		addr=$ROUTER1_GIFIP6
		remote=$ROUTER2_GIFIP6
		peernet=$ROUTER2_LANNET6
	else
		addr=$ROUTER1_GIFIP
		remote=$ROUTER2_GIFIP
		peernet=$ROUTER2_LANNET
	fi
	if [ ${outer} = "ipv6" ]; then
		src=$ROUTER1_WANIP6
		dst=$ROUTER2_WANIP6
	else
		src=$ROUTER1_WANIP
		dst=$ROUTER2_WANIP
	fi
	setup_if_gif $SOCK1 ${addr} ${remote} ${inner} \
		     ${src} ${dst} ${peernet}

	if [ $inner = "ipv6" ]; then
		addr=$ROUTER2_GIFIP6
		remote=$ROUTER1_GIFIP6
		peernet=$ROUTER1_LANNET6
	else
		addr=$ROUTER2_GIFIP
		remote=$ROUTER1_GIFIP
		peernet=$ROUTER1_LANNET
	fi
	if [ $outer = "ipv6" ]; then
		src=$ROUTER2_WANIP6
		dst=$ROUTER1_WANIP6
	else
		src=$ROUTER2_WANIP
		dst=$ROUTER1_WANIP
	fi
	setup_if_gif $SOCK2 ${addr} ${remote} ${inner} \
		     ${src} ${dst} ${peernet}
}

test_setup_tunnel()
{
	mode=${1}

	peernet=""
	opt=""
	if [ ${mode} = "ipv6" ]; then
		peernet=$ROUTER2_LANNET6
		opt="-inet6"
	else
		peernet=$ROUTER2_LANNET
		opt="-inet"
	fi
	export RUMP_SERVER=$SOCK1
	atf_check -s exit:0 -o match:gif0 rump.ifconfig
	atf_check -s exit:0 -o match:gif0 rump.route -nL get ${opt} ${peernet}

	if [ ${mode} = "ipv6" ]; then
		peernet=$ROUTER1_LANNET6
		opt="-inet6"
	else
		peernet=$ROUTER1_LANNET
		opt="-inet"
	fi
	export RUMP_SERVER=$SOCK2
	atf_check -s exit:0 -o match:gif0 rump.ifconfig
	atf_check -s exit:0 -o match:gif0 rump.route -nL get ${opt} ${peernet}
}

teardown_tunnel()
{
	export RUMP_SERVER=$SOCK1
	atf_check -s exit:0 rump.ifconfig gif0 deletetunnel
	atf_check -s exit:0 rump.ifconfig gif0 destroy

	export RUMP_SERVER=$SOCK2
	atf_check -s exit:0 rump.ifconfig gif0 deletetunnel
	atf_check -s exit:0 rump.ifconfig gif0 destroy
}

setup_dummy_if_gif()
{
	sock=${1}
	addr=${2}
	remote=${3}
	inner=${4}
	src=${5}
	dst=${6}

	export RUMP_SERVER=${sock}
	atf_check -s exit:0 rump.ifconfig gif1 create
	atf_check -s exit:0 rump.ifconfig gif1 tunnel ${src} ${dst}
	if [ ${inner} = "ipv6" ]; then
		atf_check -s exit:0 rump.ifconfig gif1 inet6 ${addr}/128 ${remote}
	else
		atf_check -s exit:0 rump.ifconfig gif1 inet ${addr}/32 ${remote}
	fi

	rump.ifconfig gif1
}

setup_dummy_tunnel()
{
	inner=${1}
	outer=${2}

	addr=""
	remote=""
	src=""
	dst=""

	if [ ${inner} = "ipv6" ]; then
		addr=$ROUTER1_GIFIP6_DUMMY
		remote=$ROUTER2_GIFIP6_DUMMY
	else
		addr=$ROUTER1_GIFIP_DUMMY
		remote=$ROUTER2_GIFIP_DUMMY
	fi
	if [ ${outer} = "ipv6" ]; then
		src=$ROUTER1_WANIP6_DUMMY
		dst=$ROUTER2_WANIP6_DUMMY
	else
		src=$ROUTER1_WANIP_DUMMY
		dst=$ROUTER2_WANIP_DUMMY
	fi
	setup_dummy_if_gif $SOCK1 ${addr} ${remote} ${inner} \
			   ${src} ${dst}

	if [ $inner = "ipv6" ]; then
		addr=$ROUTER2_GIFIP6_DUMMY
		remote=$ROUTER1_GIFIP6_DUMMY
	else
		addr=$ROUTER2_GIFIP_DUMMY
		remote=$ROUTER1_GIFIP_DUMMY
	fi
	if [ $outer = "ipv6" ]; then
		src=$ROUTER2_WANIP6_DUMMY
		dst=$ROUTER1_WANIP6_DUMMY
	else
		src=$ROUTER2_WANIP_DUMMY
		dst=$ROUTER1_WANIP_DUMMY
	fi
	setup_dummy_if_gif $SOCK2 ${addr} ${remote} ${inner} \
			   ${src} ${dst}
}

test_setup_dummy_tunnel()
{
	export RUMP_SERVER=$SOCK1
	atf_check -s exit:0 -o match:gif1 rump.ifconfig

	export RUMP_SERVER=$SOCK2
	atf_check -s exit:0 -o match:gif1 rump.ifconfig
}

teardown_dummy_tunnel()
{
	export RUMP_SERVER=$SOCK1
	atf_check -s exit:0 rump.ifconfig gif1 deletetunnel
	atf_check -s exit:0 rump.ifconfig gif1 destroy

	export RUMP_SERVER=$SOCK2
	atf_check -s exit:0 rump.ifconfig gif1 deletetunnel
	atf_check -s exit:0 rump.ifconfig gif1 destroy
}

setup_recursive_if_gif()
{
	sock=${1}
	gif=${2}
	addr=${3}
	remote=${4}
	inner=${5}
	src=${6}
	dst=${7}

	export RUMP_SERVER=${sock}
	atf_check -s exit:0 rump.ifconfig ${gif} create
	atf_check -s exit:0 rump.ifconfig ${gif} tunnel ${src} ${dst}
	if [ ${inner} = "ipv6" ]; then
		atf_check -s exit:0 rump.ifconfig ${gif} inet6 ${addr}/128 ${remote}
	else
		atf_check -s exit:0 rump.ifconfig ${gif} inet ${addr}/32 ${remote}
	fi

	rump.ifconfig ${gif}
}

# test in ROUTER1 only
setup_recursive_tunnels()
{
	mode=${1}

	addr=""
	remote=""
	src=""
	dst=""

	if [ ${mode} = "ipv6" ]; then
		addr=$ROUTER1_GIFIP6_RECURSIVE1
		remote=$ROUTER2_GIFIP6_RECURSIVE1
		src=$ROUTER1_GIFIP6
		dst=$ROUTER2_GIFIP6
	else
		addr=$ROUTER1_GIFIP_RECURSIVE1
		remote=$ROUTER2_GIFIP_RECURSIVE1
		src=$ROUTER1_GIFIP
		dst=$ROUTER2_GIFIP
	fi
	setup_recursive_if_gif $SOCK1 gif1 ${addr} ${remote} ${mode} \
		      ${src} ${dst}

	if [ ${mode} = "ipv6" ]; then
		addr=$ROUTER1_GIFIP6_RECURSIVE2
		remote=$ROUTER2_GIFIP6_RECURSIVE2
		src=$ROUTER1_GIFIP6_RECURSIVE1
		dst=$ROUTER2_GIFIP6_RECURSIVE1
	else
		addr=$ROUTER1_GIFIP_RECURSIVE2
		remote=$ROUTER2_GIFIP_RECURSIVE2
		src=$ROUTER1_GIFIP_RECURSIVE1
		dst=$ROUTER2_GIFIP_RECURSIVE1
	fi
	setup_recursive_if_gif $SOCK1 gif2 ${addr} ${remote} ${mode} \
		      ${src} ${dst}
}

# test in router1 only
test_recursive_check()
{
	mode=$1

	export RUMP_SERVER=$SOCK1
	if [ ${mode} = "ipv6" ]; then
		atf_check -s not-exit:0 -o ignore -e ignore \
			rump.ping6 -n -X $TIMEOUT -c 1 $ROUTER2_GIFIP6_RECURSIVE2
	else
		atf_check -s not-exit:0 -o ignore -e ignore \
			rump.ping -n -w $TIMEOUT -c 1 $ROUTER2_GIFIP_RECURSIVE2
	fi

	atf_check -o match:'gif0: recursively called too many times' \
		-x "$HIJACKING dmesg"

	$HIJACKING dmesg
}

teardown_recursive_tunnels()
{
	export RUMP_SERVER=$SOCK1
	atf_check -s exit:0 rump.ifconfig gif1 deletetunnel
	atf_check -s exit:0 rump.ifconfig gif1 destroy
	atf_check -s exit:0 rump.ifconfig gif2 deletetunnel
	atf_check -s exit:0 rump.ifconfig gif2 destroy
}

test_ping_failure()
{
	mode=$1

	export RUMP_SERVER=$SOCK1
	if [ ${mode} = "ipv6" ]; then
		atf_check -s not-exit:0 -o ignore -e ignore \
			rump.ping6 -n -X $TIMEOUT -c 1 -S $ROUTER1_LANIP6 \
			$ROUTER2_LANIP6
	else
		atf_check -s not-exit:0 -o ignore -e ignore \
			rump.ping -n -w $TIMEOUT -c 1 -I $ROUTER1_LANIP \
			$ROUTER2_LANIP
	fi

	export RUMP_SERVER=$SOCK2
	if [ ${mode} = "ipv6" ]; then
		atf_check -s not-exit:0 -o ignore -e ignore \
			rump.ping6 -n -X $TIMEOUT -c 1 -S $ROUTER2_LANIP6 \
			$ROUTER1_LANIP6
	else
		atf_check -s not-exit:0 -o ignore -e ignore \
			rump.ping -n -w $TIMEOUT -c 1 -I $ROUTER1_LANIP \
			$ROUTER2_LANIP
	fi
}

test_ping_success()
{
	mode=$1

	export RUMP_SERVER=$SOCK1
	rump.ifconfig -v gif0
	if [ ${mode} = "ipv6" ]; then
		# XXX
		# rump.ping6 rarely fails with the message that
		# "failed to get receiving hop limit".
		# This is a known issue being analyzed.
		atf_check -s exit:0 -o ignore \
			rump.ping6 -n -X $TIMEOUT -c 1 -S $ROUTER1_LANIP6 \
			$ROUTER2_LANIP6
	else
		atf_check -s exit:0 -o ignore \
			rump.ping -n -w $TIMEOUT -c 1 -I $ROUTER1_LANIP \
			$ROUTER2_LANIP
	fi
	rump.ifconfig -v gif0

	export RUMP_SERVER=$SOCK2
	rump.ifconfig -v gif0
	if [ ${mode} = "ipv6" ]; then
		atf_check -s exit:0 -o ignore \
			rump.ping6 -n -X $TIMEOUT -c 1 -S $ROUTER2_LANIP6 \
			$ROUTER1_LANIP6
	else
		atf_check -s exit:0 -o ignore \
			rump.ping -n -w $TIMEOUT -c 1 -I $ROUTER2_LANIP \
			$ROUTER1_LANIP
	fi
	rump.ifconfig -v gif0
}

test_change_tunnel_duplicate()
{
	mode=$1

	newsrc=""
	newdst=""
	if [ ${mode} = "ipv6" ]; then
		newsrc=$ROUTER1_WANIP6_DUMMY
		newdst=$ROUTER2_WANIP6_DUMMY
	else
		newsrc=$ROUTER1_WANIP_DUMMY
		newdst=$ROUTER2_WANIP_DUMMY
	fi
	export RUMP_SERVER=$SOCK1
	rump.ifconfig -v gif0
	rump.ifconfig -v gif1
	atf_check -s exit:0 -e match:SIOCSLIFPHYADDR \
		rump.ifconfig gif0 tunnel ${newsrc} ${newdst}
	rump.ifconfig -v gif0
	rump.ifconfig -v gif1

	if [ ${mode} = "ipv6" ]; then
		newsrc=$ROUTER2_WANIP6_DUMMY
		newdst=$ROUTER1_WANIP6_DUMMY
	else
		newsrc=$ROUTER2_WANIP_DUMMY
		newdst=$ROUTER1_WANIP_DUMMY
	fi
	export RUMP_SERVER=$SOCK2
	rump.ifconfig -v gif0
	rump.ifconfig -v gif1
	atf_check -s exit:0 -e match:SIOCSLIFPHYADDR \
		rump.ifconfig gif0 tunnel ${newsrc} ${newdst}
	rump.ifconfig -v gif0
	rump.ifconfig -v gif1
}

test_change_tunnel_success()
{
	mode=$1

	newsrc=""
	newdst=""
	if [ ${mode} = "ipv6" ]; then
		newsrc=$ROUTER1_WANIP6_DUMMY
		newdst=$ROUTER2_WANIP6_DUMMY
	else
		newsrc=$ROUTER1_WANIP_DUMMY
		newdst=$ROUTER2_WANIP_DUMMY
	fi
	export RUMP_SERVER=$SOCK1
	rump.ifconfig -v gif0
	atf_check -s exit:0 \
		rump.ifconfig gif0 tunnel ${newsrc} ${newdst}
	rump.ifconfig -v gif0

	if [ ${mode} = "ipv6" ]; then
		newsrc=$ROUTER2_WANIP6_DUMMY
		newdst=$ROUTER1_WANIP6_DUMMY
	else
		newsrc=$ROUTER2_WANIP_DUMMY
		newdst=$ROUTER1_WANIP_DUMMY
	fi
	export RUMP_SERVER=$SOCK2
	rump.ifconfig -v gif0
	atf_check -s exit:0 \
		rump.ifconfig gif0 tunnel ${newsrc} ${newdst}
	rump.ifconfig -v gif0
}

basic_setup()
{
	inner=$1
	outer=$2

	setup ${inner} ${outer}
	test_setup ${inner} ${outer}

	# Enable once PR kern/49219 is fixed
	#test_ping_failure

	setup_tunnel ${inner} ${outer}
	sleep 1
	test_setup_tunnel ${inner}
}

basic_test()
{
	inner=$1
	outer=$2 # not use

	test_ping_success ${inner}
}

basic_teardown()
{
	inner=$1
	outer=$2 # not use

	teardown_tunnel
	test_ping_failure ${inner}
}

ioctl_setup()
{
	inner=$1
	outer=$2

	setup ${inner} ${outer}
	test_setup ${inner} ${outer}

	# Enable once PR kern/49219 is fixed
	#test_ping_failure

	setup_tunnel ${inner} ${outer}
	setup_dummy_tunnel ${inner} ${outer}
	sleep 1
	test_setup_tunnel ${inner}
}

ioctl_test()
{
	inner=$1
	outer=$2

	test_ping_success ${inner}

	test_change_tunnel_duplicate ${outer}

	teardown_dummy_tunnel
	test_change_tunnel_success ${outer}
}

ioctl_teardown()
{
	inner=$1
	outer=$2 # not use

	teardown_tunnel
	test_ping_failure ${inner}
}

recursive_setup()
{
	inner=$1
	outer=$2

	setup ${inner} ${outer}
	test_setup ${inner} ${outer}

	# Enable once PR kern/49219 is fixed
	#test_ping_failure

	setup_tunnel ${inner} ${outer}
	setup_recursive_tunnels ${inner}
	sleep 1
	test_setup_tunnel ${inner}
}

recursive_test()
{
	inner=$1
	outer=$2 # not use

	test_recursive_check ${inner}
}

recursive_teardown()
{
	inner=$1 # not use
	outer=$2 # not use

	teardown_recursive_tunnels
	teardown_tunnel
}

add_test()
{
	category=$1
	desc=$2
	inner=$3
	outer=$4

	name="gif_${category}_${inner}over${outer}"
	fulldesc="Does ${inner} over ${outer} if_gif ${desc}"

	atf_test_case ${name} cleanup
	eval "${name}_head() { \
			atf_set \"descr\" \"${fulldesc}\"; \
			atf_set \"require.progs\" \"rump_server\"; \
		}; \
	    ${name}_body() { \
			${category}_setup ${inner} ${outer}; \
			${category}_test ${inner} ${outer}; \
			${category}_teardown ${inner} ${outer}; \
			rump_server_destroy_ifaces; \
	    }; \
	    ${name}_cleanup() { \
			$DEBUG && dump; \
			cleanup; \
		}"
	atf_add_test_case ${name}
}

add_test_allproto()
{
	category=$1
	desc=$2

	add_test ${category} "${desc}" ipv4 ipv4
	add_test ${category} "${desc}" ipv4 ipv6
	add_test ${category} "${desc}" ipv6 ipv4
	add_test ${category} "${desc}" ipv6 ipv6
}

atf_init_test_cases()
{
	add_test_allproto basic "basic tests"
	add_test_allproto ioctl "ioctl tests"
	add_test_allproto recursive "recursive check tests"
}
