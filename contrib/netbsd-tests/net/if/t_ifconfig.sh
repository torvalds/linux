# $NetBSD: t_ifconfig.sh,v 1.15 2017/01/20 08:35:33 ozaki-r Exp $
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

RUMP_SERVER1=unix://./r1
RUMP_SERVER2=unix://./r2

RUMP_FLAGS=\
"-lrumpnet -lrumpnet_net -lrumpnet_netinet -lrumpnet_netinet6 -lrumpnet_shmif"
RUMP_FLAGS="${RUMP_FLAGS} -lrumpdev"

TIMEOUT=3

anycast="[Aa][Nn][Yy][Cc][Aa][Ss][Tt]"
deprecated="[Dd][Ee][Pp][Rr][Ee][Cc][Aa][Tt][Ee][Dd]"

atf_test_case ifconfig_create_destroy cleanup
ifconfig_create_destroy_head()
{

	atf_set "descr" "tests of ifconfig create and destroy"
	atf_set "require.progs" "rump_server"
}

ifconfig_create_destroy_body()
{
	atf_check -s exit:0 rump_server ${RUMP_FLAGS} ${RUMP_SERVER1}

	export RUMP_SERVER=${RUMP_SERVER1}

	# Create and destroy (no address)
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 destroy

	# Create and destroy (with an IPv4 address)
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr shmbus
	atf_check -s exit:0 rump.ifconfig shmif0 192.168.0.1/24
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig shmif0 destroy

	# Create and destroy (with an IPv6 address)
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr shmbus
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fc00::1
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig shmif0 destroy

	unset RUMP_SERVER
}

ifconfig_create_destroy_cleanup()
{

	RUMP_SERVER=${RUMP_SERVER1} rump.halt
}

atf_test_case ifconfig_options cleanup
ifconfig_options_head()
{

	atf_set "descr" "tests of ifconfig options"
	atf_set "require.progs" "rump_server"
}

ifconfig_options_body()
{

	export RUMP_SERVER=${RUMP_SERVER1}
	atf_check -s exit:0 rump_server $RUMP_FLAGS $RUMP_SERVER1

	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 create
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 linkstr bus1
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 inet 10.0.0.1/24
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 inet6 fc00::1/64
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 up
	atf_check -s exit:0 -o ignore rump.ifconfig -w 10
	$DEBUG && rump.ifconfig shmif0

	# ifconfig [-N] interface address_family
	#   -N resolves hostnames
	atf_check -s exit:0 -o match:'inet 127.0.0.1' rump.ifconfig lo0 inet
	atf_check -s exit:0 -o match:'inet localhost' rump.ifconfig -N lo0 inet
	atf_check -s exit:0 -o match:'inet6 ::1' rump.ifconfig lo0 inet6
	atf_check -s exit:0 -o match:'inet6 localhost' rump.ifconfig -N lo0 inet6
	atf_check -s not-exit:0 -e match:'not supported' rump.ifconfig lo0 atalk
	atf_check -s not-exit:0 -e match:'not supported' rump.ifconfig -N lo0 atalk
	atf_check -s exit:0 -o ignore rump.ifconfig lo0 link
	atf_check -s exit:0 -o ignore rump.ifconfig -N lo0 link

	# ifconfig [-hLmNvz] interface
	#   -h -v shows statistics in human readable format
	atf_check -s exit:0 -o ignore rump.ifconfig -h -v lo0
	#   -L shows IPv6 lifetime
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 inet6 fc00::2 \
	    pltime 100
	$DEBUG && rump.ifconfig -L shmif0
	atf_check -s exit:0 -o match:'pltime' rump.ifconfig -L shmif0
	atf_check -s exit:0 -o match:'vltime' rump.ifconfig -L shmif0
	#   -m shows all of the supported media (not supported in shmif)
	$DEBUG && rump.ifconfig -m shmif0
	atf_check -s exit:0 -o ignore rump.ifconfig -m shmif0
	atf_check -s exit:0 -o match:'localhost' rump.ifconfig -N lo0
	atf_check -s exit:0 -o match:'0 packets' rump.ifconfig -v lo0
	atf_check -s exit:0 -o ignore rump.ping -c 1 -w $TIMEOUT localhost
	#   -z clears and shows statistics at that point
	atf_check -s exit:0 -o match:'2 packets' rump.ifconfig -z lo0
	atf_check -s exit:0 -o match:'0 packets' rump.ifconfig -v lo0

	# ifconfig -a [-bdhLNmsuvz]
	#   -a shows all interfaces in the system
	$DEBUG && rump.ifconfig -a
	atf_check -s exit:0 -o match:'shmif0' -o match:'lo0' rump.ifconfig -a
	#   -a -b shows only broadcast interfaces
	atf_check -s exit:0 -o match:'shmif0' -o not-match:'lo0' rump.ifconfig -a -b
	#   -a -d shows only down interfaces
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 down
	atf_check -s exit:0 -o match:'shmif0' rump.ifconfig -a -d
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 up
	atf_check -s exit:0 -o not-match:'shmif0' rump.ifconfig -a -d
	atf_check -s exit:0 -o match:'pltime' rump.ifconfig -a -L
	atf_check -s exit:0 -o match:'vltime' rump.ifconfig -a -L
	atf_check -s exit:0 -o match:'localhost' rump.ifconfig -a -N
	atf_check -s exit:0 -o ignore rump.ifconfig -a -m
	#   -a -s shows only interfaces connected to a network
	#   (shmif is always connected)
	$DEBUG && rump.ifconfig -a -s
	atf_check -s exit:0 -o ignore rump.ifconfig -a -s
	#   -a -u shows only up interfaces
	atf_check -s exit:0 -o match:'shmif0' rump.ifconfig -a -u
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 down
	atf_check -s exit:0 -o not-match:'shmif0' rump.ifconfig -a -u
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 up
	atf_check -s exit:0 -o match:'0 packets' rump.ifconfig -a -v
	atf_check -s exit:0 -o ignore rump.ping -c 1 -w $TIMEOUT localhost
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 down
	atf_check -s exit:0 -o match:'2 packets' rump.ifconfig -a -z
	atf_check -s exit:0 -o not-match:'2 packets' rump.ifconfig -a -v
	atf_check -s exit:0 -o match:'0 packets' rump.ifconfig -a -v
	atf_check -s exit:0 -o ignore rump.ifconfig shmif0 up

	# ifconfig -l [-bdsu]
	#   -l shows only inteface names
	atf_check -s exit:0 -o match:'lo0' rump.ifconfig -l
	atf_check -s exit:0 -o match:'shmif0' rump.ifconfig -l
	atf_check -s exit:0 -o match:'shmif0' rump.ifconfig -l -b
	atf_check -s exit:0 -o not-match:'lo0' rump.ifconfig -l -b
	atf_check -s exit:0 -o ignore rump.ifconfig -l -d
	atf_check -s exit:0 -o match:'lo0' rump.ifconfig -l -s
	atf_check -s exit:0 -o match:'shmif0' rump.ifconfig -l -s
	atf_check -s exit:0 -o match:'lo0' rump.ifconfig -l -u
	atf_check -s exit:0 -o match:'shmif0' rump.ifconfig -l -u

	# ifconfig -s interface
	#   -s interface exists with 0 / 1 if connected / disconnected
	atf_check -s exit:0 -o empty rump.ifconfig -s lo0
	atf_check -s exit:0 -o empty rump.ifconfig -s shmif0

	# ifconfig -C
	#   -C shows all of the interface cloners available on the system
	atf_check -s exit:0 -o match:'shmif carp lo' rump.ifconfig -C

	unset RUMP_SERVER
}

ifconfig_options_cleanup()
{

	env RUMP_SERVER=${RUMP_SERVER1} rump.halt
}


atf_test_case ifconfig_parameters cleanup
ifconfig_parameters_head()
{
	atf_set "descr" "tests of interface parameters"
	atf_set "require.progs" "rump_server"
}

ifconfig_parameters_body()
{
	local interval=

	atf_check -s exit:0 rump_server ${RUMP_FLAGS} ${RUMP_SERVER1}
	atf_check -s exit:0 rump_server ${RUMP_FLAGS} ${RUMP_SERVER2}

	export RUMP_SERVER=${RUMP_SERVER1}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr shmbus
	atf_check -s exit:0 rump.ifconfig shmif0 192.168.0.1/24
	atf_check -s exit:0 rump.ifconfig shmif0 up
	unset RUMP_SERVER

	export RUMP_SERVER=${RUMP_SERVER2}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr shmbus
	atf_check -s exit:0 rump.ifconfig shmif0 192.168.0.2/24
	atf_check -s exit:0 rump.ifconfig shmif0 inet 192.168.0.3/24 alias
	atf_check -s exit:0 rump.ifconfig shmif0 up
	unset RUMP_SERVER

	export RUMP_SERVER=${RUMP_SERVER1}

	# active
	atf_check -s exit:0 rump.ifconfig shmif0 link b2:a0:75:00:00:01 active
	atf_check -s exit:0 -o match:'address:.b2:a0:75:00:00:01' \
	    rump.ifconfig shmif0
	# down, up
	atf_check -s exit:0 rump.ifconfig shmif0 down
	atf_check -s not-exit:0 -o ignore -e ignore rump.ping -c 1 \
	    -w $TIMEOUT -n 192.168.0.2
	atf_check -s exit:0 rump.ifconfig shmif0 up
	atf_check -s exit:0 rump.ifconfig -w 10
	atf_check -s exit:0 -o ignore rump.ping -c 1 -w $TIMEOUT -n 192.168.0.2

	# alias
	atf_check -s exit:0 rump.ifconfig shmif0 inet 192.168.1.1/24 alias
	atf_check -s exit:0 -o match:'192.168.1.1/24' rump.ifconfig shmif0
	atf_check -s exit:0 rump.ifconfig shmif0 inet 192.168.1.1/24 -alias
	atf_check -s exit:0 -o not-match:'192.168.1.1/24' rump.ifconfig shmif0
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fc00::1
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fc00::2
	atf_check -s exit:0 -o match:'fc00::1' rump.ifconfig shmif0 inet6
	atf_check -s exit:0 -o match:'fc00::2' rump.ifconfig shmif0 inet6

	# delete
	atf_check -s exit:0 rump.ifconfig shmif0 inet 192.168.1.1 alias
	atf_check -s exit:0 rump.ifconfig shmif0 inet 192.168.1.1 delete
	atf_check -s exit:0 -o not-match:'192.168.1.1' rump.ifconfig shmif0 inet
	atf_check -s exit:0 rump.ifconfig shmif0 inet 192.168.0.1 delete
	atf_check -s exit:0 -o not-match:'192.168.0.1' rump.ifconfig shmif0 inet
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fc00::1 delete
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fc00::2 delete
	atf_check -s exit:0 -o not-match:'fc00::1' rump.ifconfig shmif0 inet6
	atf_check -s exit:0 -o not-match:'fc00::2' rump.ifconfig shmif0 inet6
	# can delete inactive link
	atf_check -s exit:0 rump.ifconfig shmif0 link b2:a0:75:00:00:02
	atf_check -s exit:0 rump.ifconfig shmif0 link b2:a0:75:00:00:02 delete
	# cannot delete active link
	atf_check -s not-exit:0 -e match:'SIOCDLIFADDR: Device busy' \
	    rump.ifconfig shmif0 link b2:a0:75:00:00:01 delete

	atf_check -s exit:0 rump.ifconfig shmif0 inet 192.168.0.1/24

	# arp
	atf_check -s exit:0 rump.ifconfig shmif0 -arp
	atf_check -s not-exit:0 -o ignore -e ignore \
	    rump.ping -c 1 -w $TIMEOUT -n 192.168.0.3
	atf_check -s exit:0 -o not-match:'192.168.0.3' rump.arp -an
	# The entry shouldn't appear in the routing table anymore
	atf_check -s exit:0 -o not-match:'192.168.0.3' rump.netstat -nr

	# netmask
	atf_check -s exit:0 rump.ifconfig shmif0 inet 172.16.0.1 netmask 255.255.255.0 alias
	atf_check -s exit:0 -o match:'172.16.0/24' rump.netstat -rn -f inet
	atf_check -s exit:0 rump.ifconfig shmif0 inet 172.16.0.1 delete

	# broadcast (does it not work?)
	atf_check -s exit:0 rump.ifconfig shmif0 inet 172.16.0.1 \
	    broadcast 255.255.255.255 alias
	atf_check -s exit:0 -o match:'broadcast 255.255.255.255' \
	    rump.ifconfig shmif0 inet

	# metric (external only)
	atf_check -s exit:0 rump.ifconfig shmif0 metric 10
	atf_check -s exit:0 rump.ifconfig shmif0 metric 0

	# prefixlen
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fc00::1 prefixlen 70
	atf_check -s exit:0 -o match:'fc00::/70' rump.netstat -rn -f inet6

	# anycast
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fc00::2 anycast
	atf_check -s exit:0 -o match:"fc00::2.+$anycast" rump.ifconfig shmif0 inet6

	# deprecated
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fc00::3 deprecated
	# Not deprecated immediately. Need to wait nd6_timer that does it is scheduled.
	interval=$(sysctl -n net.inet6.icmp6.nd6_prune)
	atf_check -s exit:0 sleep $((interval + 1))
	atf_check -s exit:0 -o match:"fc00::3.+$deprecated" rump.ifconfig shmif0 inet6
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fc00::3 -deprecated
	atf_check -s exit:0 -o not-match:"fc00::3.+$deprecated" rump.ifconfig shmif0 inet6

	# pltime
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fc00::3 pltime 3
	atf_check -s exit:0 -o not-match:"fc00::3.+$deprecated" rump.ifconfig shmif0 inet6
	atf_check -s exit:0 sleep 5
	atf_check -s exit:0 -o match:"fc00::3.+$deprecated" rump.ifconfig shmif0 inet6

	# eui64
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fc00:1::0 eui64
	atf_check -s exit:0 -o match:'fc00:1::' rump.ifconfig shmif0 inet6

	unset RUMP_SERVER
}

ifconfig_parameters_cleanup()
{
	env RUMP_SERVER=${RUMP_SERVER1} rump.halt
	env RUMP_SERVER=${RUMP_SERVER2} rump.halt
}

atf_init_test_cases()
{

	atf_add_test_case ifconfig_create_destroy
	atf_add_test_case ifconfig_options
	atf_add_test_case ifconfig_parameters
}
