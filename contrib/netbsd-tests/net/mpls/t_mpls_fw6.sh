# $NetBSD: t_mpls_fw6.sh,v 1.3 2016/08/10 07:50:37 ozaki-r Exp $
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

# Test MPLS encap/decap and forwarding using INET6 as encapsulated protocol
# Setup four routers connected like this: R1---R2---R3---R4--
# Goal is to be able to ping from R1 the outermost interface of R4
# Disable net.inet6.ip6.forwarding, enable net.mpls.forwarding
# Add route on R1 in order to encapsulate into MPLS the IP6 packets with
#     destination equal to R4 right hand side interface
# Add MPLS routes on R2 in order to forward frames belonging to that FEC to R3
# Add MPLS "POP" route on R3 for that FEC, pointing to R4
# Do the same for the reverse direction (R4 to R1)
# ping6 from R1 to R4 right hand side interface
#
# redo the test using IPv6 explicit null label

RUMP_SERVER1=unix://./r1
RUMP_SERVER2=unix://./r2
RUMP_SERVER3=unix://./r3
RUMP_SERVER4=unix://./r4

RUMP_FLAGS6="-lrumpnet -lrumpnet_net -lrumpnet_netinet -lrumpnet_netinet6 \
             -lrumpdev -lrumpnet_shmif -lrumpnet_netmpls"

atf_test_case mplsfw6 cleanup
mplsfw6_head()
{

	atf_set "descr" "IP6/MPLS forwarding test using PHP"
	atf_set "require.progs" "rump_server"
}

startservers()
{

	ulimit -r 300
	atf_check -s exit:0 rump_server ${RUMP_FLAGS6} ${RUMP_SERVER1}
	atf_check -s exit:0 rump_server ${RUMP_FLAGS6} ${RUMP_SERVER2}
	atf_check -s exit:0 rump_server ${RUMP_FLAGS6} ${RUMP_SERVER3}
	atf_check -s exit:0 rump_server ${RUMP_FLAGS6} ${RUMP_SERVER4}
}

configservers()
{

	# Setup the first server
	export RUMP_SERVER=${RUMP_SERVER1}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr ./shdom1
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fd00:1234::1/64 alias
	atf_check -s exit:0 rump.ifconfig mpls0 create up
	atf_check -s exit:0 rump.sysctl -q -w net.mpls.accept=1
	atf_check -s exit:0 rump.sysctl -q -w net.inet6.ip6.forwarding=0
	atf_check -s exit:0 rump.route -q add -inet6 fd00:1234:0:3::/64 \
	    -ifa fd00:1234::1 \
	    -ifp mpls0 -tag 25 -inet6 fd00:1234::2

	# Setup the second server
	export RUMP_SERVER=${RUMP_SERVER2}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr ./shdom1
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fd00:1234::2/64 alias
	atf_check -s exit:0 rump.ifconfig shmif1 create
	atf_check -s exit:0 rump.ifconfig shmif1 linkstr ./shdom2
	atf_check -s exit:0 rump.ifconfig shmif1 inet6 fd00:1234:0:1::1/64 alias
	atf_check -s exit:0 rump.ifconfig mpls0 create up
	atf_check -s exit:0 rump.sysctl -q -w net.mpls.accept=1
	atf_check -s exit:0 rump.sysctl -q -w net.mpls.forwarding=1
	atf_check -s exit:0 rump.sysctl -q -w net.inet6.ip6.forwarding=0
	atf_check -s exit:0 rump.route -q add -mpls 25 -tag 30 \
	    -inet6 fd00:1234:0:1::2
	atf_check -s exit:0 rump.route -q add -mpls 27 -tag ${1} -inet6 \
	    fd00:1234::1

	# Setup the third server
	export RUMP_SERVER=${RUMP_SERVER3}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr ./shdom2
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fd00:1234:0:1::2/64 alias
	atf_check -s exit:0 rump.ifconfig shmif1 create
	atf_check -s exit:0 rump.ifconfig shmif1 linkstr ./shdom3
	atf_check -s exit:0 rump.ifconfig shmif1 inet6 fd00:1234:0:2::1/64 alias
	atf_check -s exit:0 rump.ifconfig mpls0 create up
	atf_check -s exit:0 rump.sysctl -q -w net.mpls.accept=1
	atf_check -s exit:0 rump.sysctl -q -w net.mpls.forwarding=1
	atf_check -s exit:0 rump.sysctl -q -w net.inet6.ip6.forwarding=0
	atf_check -s exit:0 rump.route -q add -mpls 30 -tag ${1} \
	    -inet6 fd00:1234:0:2::2
	atf_check -s exit:0 rump.route -q add -mpls 26 -tag 27 \
	    -inet6 fd00:1234:0:1::1

	# Setup the fourth server
	export RUMP_SERVER=${RUMP_SERVER4}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr ./shdom3
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fd00:1234:0:2::2/64 alias
	atf_check -s exit:0 rump.ifconfig shmif1 create
	atf_check -s exit:0 rump.ifconfig shmif1 linkstr ./shdom4
	atf_check -s exit:0 rump.ifconfig shmif1 inet6 fd00:1234:0:3::1/64 alias
	atf_check -s exit:0 rump.ifconfig mpls0 create up
	atf_check -s exit:0 rump.sysctl -q -w net.mpls.accept=1
	atf_check -s exit:0 rump.sysctl -q -w net.inet6.ip6.forwarding=0
	atf_check -s exit:0 rump.route -q add -inet6 fd00:1234::/64 \
	    -ifa fd00:1234:0:2::2 \
	    -ifp mpls0 -tag 26 -inet6 fd00:1234:0:2::1

	unset RUMP_SERVER
}

doping()
{

	export RUMP_SERVER=${RUMP_SERVER1}
	atf_check -s exit:0 \
	    -o match:" bytes from fd00:1234::2, icmp_seq=" \
	    rump.ping6 -n -o -X 2 fd00:1234::2
	export RUMP_SERVER=${RUMP_SERVER2}
	atf_check -s exit:0 \
	    -o match:" bytes from fd00:1234:0:1::2, icmp_seq=" \
	    rump.ping6 -n -o -X 2 fd00:1234:0:1::2
	export RUMP_SERVER=${RUMP_SERVER3}
	atf_check -s exit:0 \
	    -o match:" bytes from fd00:1234:0:2::2, icmp_seq=" \
	    rump.ping6 -n -o -X 2 fd00:1234:0:2::2
	export RUMP_SERVER=${RUMP_SERVER1}
	atf_check -s exit:0 \
	    -o match:" bytes from fd00:1234:0:3::1, icmp_seq=" \
	    rump.ping6 -n -o -X 2 fd00:1234:0:3::1
	unset RUMP_SERVER
}

do_check_route()
{

	export RUMP_SERVER=${RUMP_SERVER1}
	atf_check -s exit:0 \
	    -o match:"^fd00:1234:0:3::/64.+fd00:1234::2.+25.+mpls0" \
	    rump.netstat -nrT
	unset RUMP_SERVER
}

docleanup()
{

	RUMP_SERVER=${RUMP_SERVER1} rump.halt
	RUMP_SERVER=${RUMP_SERVER2} rump.halt
	RUMP_SERVER=${RUMP_SERVER3} rump.halt
	RUMP_SERVER=${RUMP_SERVER4} rump.halt
}

mplsfw6_body()
{

	startservers
	configservers 3
	do_check_route
	doping
}

mplsfw6_cleanup()
{

	docleanup
}


atf_test_case mplsfw6_expl cleanup
mplsfw4_expl_head()
{

	atf_set "descr" "IP6/MPLS forwarding test using explicit NULL labels"
	atf_set "require.progs" "rump_server"
}

mplsfw6_expl_body()
{

	startservers
	configservers 2
	do_check_route
	doping
}

mplsfw6_expl_cleanup()
{

	docleanup
}


atf_init_test_cases()
{ 

	atf_add_test_case mplsfw6
	atf_add_test_case mplsfw6_expl
} 
