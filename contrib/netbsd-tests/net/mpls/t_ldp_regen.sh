# $NetBSD: t_ldp_regen.sh,v 1.7 2016/08/10 07:50:37 ozaki-r Exp $
#
# Copyright (c) 2013 The NetBSD Foundation, Inc.
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

# IP/MPLS & LDP label reallocation test
# Create 4 routers connected like this: R1--R2--R3--R4--
# The goal is to push packets from R1 to the R4 shmif1 (the right one) interface
# Enable MPLS forwarding on R2
# Disable IP forwarding and enable MPLS forwarding on R3
# Start ldpd and wait for adjancencies to come up
# Add an alias on shmif1 on R4 for which we already have a route on R3
# Now: * R4 should install label IMPLNULL for that prefix
#      * R3 should realloc the target label from IMPLNULL to something else


RUMP_SERVER1=unix://./r1
RUMP_SERVER2=unix://./r2
RUMP_SERVER3=unix://./r3
RUMP_SERVER4=unix://./r4

RUMP_LIBS="-lrumpnet -lrumpnet_net -lrumpnet_netinet -lrumpnet_netinet6 \
           -lrumpdev -lrumpnet_netmpls -lrumpnet_shmif"
LDP_FLAGS=""

atf_test_case ldp_regen cleanup
ldp_regen_head() {

	atf_set "descr" "IP/MPLS and LDP label regeneration test"
	atf_set "require.progs" "rump_server"
	atf_set "use.fs" "true"
}

newaddr_and_ping() {

	# Add new address on R4
	RUMP_SERVER=${RUMP_SERVER4} atf_check -s exit:0 \
		rump.ifconfig shmif1 10.0.5.1/24 alias
	RUMP_SERVER=${RUMP_SERVER4} atf_check -s exit:0 \
		rump.ifconfig -w 60

	# Now ldpd on R5 should take notice of the new route and announce it
	# to R4's ldpd. ldpd on R4 should verify that the next hop
	# corresponds to its routing table and change its tag entry
	RUMP_SERVER=${RUMP_SERVER1} atf_check -s exit:0 -o ignore -e ignore \
		rump.ping -n -o -w 5 10.0.5.1
}

create_servers() {

	# allows us to run as normal user
	ulimit -r 400

	atf_check -s exit:0 rump_server ${RUMP_LIBS} ${RUMP_SERVER1}
	atf_check -s exit:0 rump_server ${RUMP_LIBS} ${RUMP_SERVER2}
	atf_check -s exit:0 rump_server ${RUMP_LIBS} ${RUMP_SERVER3}
	atf_check -s exit:0 rump_server ${RUMP_LIBS} ${RUMP_SERVER4}

	# LDP HIJACK
	export RUMPHIJACK=path=/rump,socket=all,sysctl=yes
	export LD_PRELOAD=/usr/lib/librumphijack.so

	# Setup first server
	export RUMP_SERVER=${RUMP_SERVER1}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr ./shdom1
	atf_check -s exit:0 rump.ifconfig shmif0 10.0.1.1/24
	atf_check -s exit:0 rump.ifconfig mpls0 create up
	atf_check -s exit:0 rump.sysctl -q -w net.inet.ip.forwarding=0
	atf_check -s exit:0 rump.route -q add 10.0.4.0/24 10.0.1.2
	atf_check -s exit:0 rump.route -q add 10.0.5.0/24 10.0.1.2
	atf_check -s exit:0 /usr/sbin/ldpd ${LDP_FLAGS}

	# Setup second server
	export RUMP_SERVER=${RUMP_SERVER2}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr ./shdom1
	atf_check -s exit:0 rump.ifconfig shmif0 10.0.1.2/24
	atf_check -s exit:0 rump.ifconfig shmif1 create
	atf_check -s exit:0 rump.ifconfig shmif1 linkstr ./shdom2
	atf_check -s exit:0 rump.ifconfig shmif1 10.0.2.1/24
	atf_check -s exit:0 rump.ifconfig mpls0 create up
	atf_check -s exit:0 rump.sysctl -q -w net.mpls.accept=1
	atf_check -s exit:0 rump.sysctl -q -w net.mpls.forwarding=1
	# This one should still do ip forwarding because it announces IMPLNULL
	# for the 10.0.1.0/24 subnet
	atf_check -s exit:0 rump.route -q add 10.0.4.0/24 10.0.2.2
	atf_check -s exit:0 rump.route -q add 10.0.5.0/24 10.0.2.2
	atf_check -s exit:0 /usr/sbin/ldpd ${LDP_FLAGS}

	# Setup third server
	export RUMP_SERVER=${RUMP_SERVER3}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr ./shdom2
	atf_check -s exit:0 rump.ifconfig shmif0 10.0.2.2/24
	atf_check -s exit:0 rump.ifconfig shmif1 create
	atf_check -s exit:0 rump.ifconfig shmif1 linkstr ./shdom3
	atf_check -s exit:0 rump.ifconfig shmif1 10.0.3.1/24
	atf_check -s exit:0 rump.ifconfig mpls0 create up
	atf_check -s exit:0 rump.sysctl -q -w net.mpls.accept=1
	atf_check -s exit:0 rump.sysctl -q -w net.mpls.forwarding=1
	atf_check -s exit:0 rump.sysctl -q -w net.inet.ip.forwarding=0
	atf_check -s exit:0 rump.route -q add 10.0.1.0/24 10.0.2.1
	atf_check -s exit:0 rump.route -q add 10.0.4.0/24 10.0.3.2
	atf_check -s exit:0 rump.route -q add 10.0.5.0/24 10.0.3.2
	atf_check -s exit:0 /usr/sbin/ldpd ${LDP_FLAGS}

	# Setup fourth server
	export RUMP_SERVER=${RUMP_SERVER4}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr ./shdom3
	atf_check -s exit:0 rump.ifconfig shmif0 10.0.3.2/24
	atf_check -s exit:0 rump.ifconfig shmif1 create
	atf_check -s exit:0 rump.ifconfig shmif1 linkstr ./shdom4
	atf_check -s exit:0 rump.ifconfig shmif1 10.0.4.1/24
	atf_check -s exit:0 rump.sysctl -q -w net.inet.ip.forwarding=0
	atf_check -s exit:0 rump.ifconfig mpls0 create up
	atf_check -s exit:0 rump.route -q add 10.0.1.0/24 10.0.3.1
	atf_check -s exit:0 /usr/sbin/ldpd ${LDP_FLAGS}

	unset RUMP_SERVER
	unset LD_PRELOAD
	unset RUMPHIJACK
}

wait_ldp_ok() {

	RUMP_SERVER=${RUMP_SERVER1} atf_check -s exit:0 -o ignore -e ignore \
		rump.ifconfig -w 60
	RUMP_SERVER=${RUMP_SERVER1} atf_check -s exit:0 -o ignore -e ignore \
		rump.ping -o -w 60 10.0.4.1
}

docleanup() {

	RUMP_SERVER=${RUMP_SERVER1} rump.halt
	RUMP_SERVER=${RUMP_SERVER2} rump.halt
	RUMP_SERVER=${RUMP_SERVER3} rump.halt
	RUMP_SERVER=${RUMP_SERVER4} rump.halt
}

ldp_regen_body() {

        if sysctl machdep.cpu_brand 2>/dev/null | grep QEMU >/dev/null 2>&1
	then
	    atf_skip "unreliable under qemu, skip until PR kern/43997 fixed"
	fi
	create_servers
	wait_ldp_ok
	newaddr_and_ping
}

ldp_regen_cleanup() {

	docleanup
}

atf_init_test_cases() {

	atf_add_test_case ldp_regen
}
