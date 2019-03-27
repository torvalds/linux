#	$NetBSD: t_change.sh,v 1.9 2016/11/07 05:25:37 ozaki-r Exp $
#
# Copyright (c) 2011 The NetBSD Foundation, Inc.
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

netserver=\
"rump_server -lrumpdev -lrumpnet -lrumpnet_net \
	-lrumpnet_netinet -lrumpnet_shmif"
export RUMP_SERVER=unix://commsock

DEBUG=${DEBUG:-false}

atf_test_case route_change_reject2blackhole cleanup
route_change_reject2blackhole_head()
{

	atf_set "descr" "Change a reject route to blackhole"
	atf_set "require.progs" "rump_server"
}

route_change_reject2blackhole_body()
{

	atf_check -s exit:0 ${netserver} ${RUMP_SERVER}

	atf_check -s exit:0 -o ignore \
	    rump.route add 207.46.197.32 127.0.0.1 -reject
	atf_check -s exit:0 -o match:UGHR -x \
	    "rump.route -n show -inet | grep ^207.46"
	atf_check -s exit:0 -o ignore \
	    rump.route change 207.46.197.32 127.0.0.1 -blackhole
	atf_check -s exit:0 -o match:' UGHBS ' -e ignore -x \
	    "rump.netstat -rn -f inet | grep ^207.46"
}

route_change_reject2blackhole_cleanup()
{

	env RUMP_SERVER=unix://commsock rump.halt
}

atf_test_case route_change_gateway cleanup
route_change_gateway_head()
{

	atf_set "descr" "Change the gateway of a route"
	atf_set "require.progs" "rump_server"
}

route_change_gateway_body()
{

	atf_check -s exit:0 ${netserver} ${RUMP_SERVER}

	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr bus
	atf_check -s exit:0 rump.ifconfig shmif0 10.0.0.10/24 up

	atf_check -s exit:0 -o ignore \
	    rump.route add -net 192.168.0.0/24 10.0.0.1
	atf_check -s exit:0 -o match:'10.0.0.1' -x \
	    "rump.route -n show -inet | grep ^192.168"
	atf_check -s exit:0 -o ignore \
	    rump.route change -net 192.168.0.0/24 10.0.0.254
	atf_check -s exit:0 -o match:'10.0.0.254' -x \
	    "rump.route -n show -inet | grep ^192.168"
}

route_change_gateway_cleanup()
{

	env RUMP_SERVER=unix://commsock rump.halt
}

atf_test_case route_change_ifa cleanup
route_change_ifa_head()
{

	atf_set "descr" "Change the ifa (local address) of a route"
	atf_set "require.progs" "rump_server"
}

route_change_ifa_body()
{

	atf_check -s exit:0 ${netserver} ${RUMP_SERVER}

	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr bus
	atf_check -s exit:0 rump.ifconfig shmif0 10.0.0.10/24
	atf_check -s exit:0 rump.ifconfig shmif0 alias 10.0.0.11/24
	atf_check -s exit:0 rump.ifconfig shmif0 up

	atf_check -s exit:0 -o ignore \
	    rump.route add -net 192.168.0.0/24 10.0.0.1
	atf_check -s exit:0 -o match:'10.0.0.1' -x \
	    "rump.route -n show -inet | grep ^192.168"
	$DEBUG && rump.route -n show -inet
	cat >./expect <<-EOF
   route to: 192.168.0.1
destination: 192.168.0.0
       mask: 255.255.255.0
    gateway: 10.0.0.1
 local addr: 10.0.0.10
  interface: shmif0
      flags: <UP,GATEWAY,DONE,STATIC>
 recvpipe  sendpipe  ssthresh  rtt,msec    rttvar  hopcount      mtu     expire
	EOF
	rump.route -n get 192.168.0.1 > ./output
	$DEBUG && cat ./expect ./output
	sed -i '$d' ./output
	atf_check -s exit:0 diff ./expect ./output

	# Change the local address of the route
	atf_check -s exit:0 -o ignore \
	    rump.route change -net 192.168.0.0/24 10.0.0.1 -ifa 10.0.0.11
	$DEBUG && rump.route -n show -inet
	cat >./expect <<-EOF
   route to: 192.168.0.1
destination: 192.168.0.0
       mask: 255.255.255.0
    gateway: 10.0.0.1
 local addr: 10.0.0.11
  interface: shmif0
      flags: <UP,GATEWAY,DONE,STATIC>
 recvpipe  sendpipe  ssthresh  rtt,msec    rttvar  hopcount      mtu     expire
	EOF
	rump.route -n get 192.168.0.1 > ./output
	$DEBUG && cat ./expect ./output
	sed -i '$d' ./output
	atf_check -s exit:0 diff ./expect ./output
}

route_change_ifa_cleanup()
{

	env RUMP_SERVER=unix://commsock rump.halt
}

atf_test_case route_change_ifp cleanup
route_change_ifp_head()
{

	atf_set "descr" "Change a route based on an interface (ifp)"
	atf_set "require.progs" "rump_server"
}

route_change_ifp_body()
{

	atf_check -s exit:0 ${netserver} ${RUMP_SERVER}

	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr bus
	atf_check -s exit:0 rump.ifconfig shmif0 10.0.0.10/24 up

	atf_check -s exit:0 rump.ifconfig shmif1 create
	atf_check -s exit:0 rump.ifconfig shmif1 linkstr bus
	atf_check -s exit:0 rump.ifconfig shmif1 10.0.0.11/24 up

	atf_check -s exit:0 -o ignore \
	    rump.route add -net 192.168.0.0/24 10.0.0.1
	atf_check -s exit:0 -o match:'10.0.0.1' -x \
	    "rump.route -n show -inet | grep ^192.168"
	$DEBUG && rump.route -n show -inet
	cat >./expect <<-EOF
   route to: 192.168.0.1
destination: 192.168.0.0
       mask: 255.255.255.0
    gateway: 10.0.0.1
 local addr: 10.0.0.10
  interface: shmif0
      flags: <UP,GATEWAY,DONE,STATIC>
 recvpipe  sendpipe  ssthresh  rtt,msec    rttvar  hopcount      mtu     expire
	EOF
	rump.route -n get 192.168.0.1 > ./output
	$DEBUG && cat ./expect ./output
	sed -i '$d' ./output
	atf_check -s exit:0 diff ./expect ./output

	# Change a route based on an interface
	atf_check -s exit:0 -o ignore \
	    rump.route change -net 192.168.0.0/24 10.0.0.1 -ifp shmif1
	$DEBUG && rump.route -n show -inet
	cat >./expect <<-EOF
   route to: 192.168.0.1
destination: 192.168.0.0
       mask: 255.255.255.0
    gateway: 10.0.0.1
 local addr: 10.0.0.11
  interface: shmif1
      flags: <UP,GATEWAY,DONE,STATIC>
 recvpipe  sendpipe  ssthresh  rtt,msec    rttvar  hopcount      mtu     expire
	EOF
	rump.route -n get 192.168.0.1 > ./output
	$DEBUG && cat ./expect ./output
	sed -i '$d' ./output
	atf_check -s exit:0 diff ./expect ./output
}

route_change_ifp_cleanup()
{

	env RUMP_SERVER=unix://commsock rump.halt
}

atf_test_case route_change_ifp_ifa cleanup
route_change_ifp_head()
{

	atf_set "descr" "Change a route with -ifp and -ifa"
	atf_set "require.progs" "rump_server"
}

route_change_ifp_ifa_body()
{

	atf_check -s exit:0 ${netserver} ${RUMP_SERVER}

	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr bus
	atf_check -s exit:0 rump.ifconfig shmif0 10.0.0.10/24 up

	atf_check -s exit:0 rump.ifconfig shmif1 create
	atf_check -s exit:0 rump.ifconfig shmif1 linkstr bus
	atf_check -s exit:0 rump.ifconfig shmif1 10.0.0.11/24 up

	atf_check -s exit:0 -o ignore \
	    rump.route add -net 192.168.0.0/24 10.0.0.1
	atf_check -s exit:0 -o match:'10.0.0.1' -x \
	    "rump.route -n show -inet | grep ^192.168"
	$DEBUG && rump.route -n show -inet
	cat >./expect <<-EOF
   route to: 192.168.0.1
destination: 192.168.0.0
       mask: 255.255.255.0
    gateway: 10.0.0.1
 local addr: 10.0.0.10
  interface: shmif0
      flags: <UP,GATEWAY,DONE,STATIC>
 recvpipe  sendpipe  ssthresh  rtt,msec    rttvar  hopcount      mtu     expire
	EOF
	rump.route -n get 192.168.0.1 > ./output
	$DEBUG && cat ./expect ./output
	sed -i '$d' ./output
	atf_check -s exit:0 diff ./expect ./output

	# Change a route with -ifa and -ifp
	atf_check -s exit:0 -o ignore \
	    rump.route change -net 192.168.0.0/24 -ifa 10.0.0.1 -ifp shmif1
	$DEBUG && rump.route -n show -inet
	cat >./expect <<-EOF
   route to: 192.168.0.1
destination: 192.168.0.0
       mask: 255.255.255.0
    gateway: 10.0.0.1
 local addr: 10.0.0.11
  interface: shmif1
      flags: <UP,GATEWAY,DONE,STATIC>
 recvpipe  sendpipe  ssthresh  rtt,msec    rttvar  hopcount      mtu     expire
	EOF
	rump.route -n get 192.168.0.1 > ./output
	$DEBUG && cat ./expect ./output
	sed -i '$d' ./output
	atf_check -s exit:0 diff ./expect ./output
}

route_change_ifp_ifa_cleanup()
{

	env RUMP_SERVER=unix://commsock rump.halt
}

atf_init_test_cases()
{

	atf_add_test_case route_change_reject2blackhole
	atf_add_test_case route_change_gateway
	atf_add_test_case route_change_ifa
	atf_add_test_case route_change_ifp
	atf_add_test_case route_change_ifp_ifa
}
