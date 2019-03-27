#	$NetBSD: t_tun.sh,v 1.4 2016/11/07 05:25:37 ozaki-r Exp $
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

RUMP_FLAGS="-lrumpnet -lrumpnet_net -lrumpnet_netinet -lrumpnet_netinet6"
RUMP_FLAGS="$RUMP_FLAGS -lrumpnet_shmif -lrumpnet_tun -lrumpdev"

BUS=bus
SOCK_LOCAL=unix://commsock1
SOCK_REMOTE=unix://commsock2
IP_LOCAL=10.0.0.1
IP_REMOTE=10.0.0.2

DEBUG=${DEBUG:-true}

atf_test_case tun_create_destroy cleanup
tun_create_destroy_head()
{

	atf_set "descr" "tests of creation and deletion of tun interface"
	atf_set "require.progs" "rump_server"
}

tun_create_destroy_body()
{

	atf_check -s exit:0 rump_server ${RUMP_FLAGS} ${SOCK_LOCAL}

	export RUMP_SERVER=${SOCK_LOCAL}

	atf_check -s exit:0 rump.ifconfig tun0 create
	atf_check -s exit:0 rump.ifconfig tun0 up
	atf_check -s exit:0 rump.ifconfig tun0 down
	atf_check -s exit:0 rump.ifconfig tun0 destroy
}

tun_create_destroy_cleanup()
{

	RUMP_SERVER=${SOCK_LOCAL} rump.halt
}

atf_test_case tun_setup cleanup
tun_setup_head()
{

	atf_set "descr" "tests of setting up a tunnel"
	atf_set "require.progs" "rump_server"
}

check_route_entry()
{
	local ip=$(echo $1 |sed 's/\./\\./g')
	local gw=$2
	local flags=$3
	local iface=$4

	atf_check -s exit:0 -o match:" $flags " -e ignore -x \
	    "rump.netstat -rn -f inet | grep ^'$ip'"
	atf_check -s exit:0 -o match:" $gw " -e ignore -x \
	    "rump.netstat -rn -f inet | grep ^'$ip'"
	atf_check -s exit:0 -o match:" $iface" -e ignore -x \
	    "rump.netstat -rn -f inet | grep ^'$ip'"
}

tun_setup_body()
{

	atf_check -s exit:0 rump_server ${RUMP_FLAGS} ${SOCK_LOCAL}
	atf_check -s exit:0 rump_server ${RUMP_FLAGS} ${SOCK_REMOTE}

	export RUMP_SERVER=${SOCK_LOCAL}

	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr $BUS
	atf_check -s exit:0 rump.ifconfig shmif0 ${IP_LOCAL}/24 up
	atf_check -s exit:0 rump.ifconfig -w 10

	export RUMP_SERVER=${SOCK_REMOTE}

	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr $BUS
	atf_check -s exit:0 rump.ifconfig shmif0 ${IP_REMOTE}/24 up
	atf_check -s exit:0 rump.ifconfig -w 10

	export RUMP_SERVER=${SOCK_LOCAL}
	atf_check -s exit:0 rump.ifconfig tun0 create
	atf_check -s exit:0 rump.ifconfig tun0 ${IP_LOCAL} ${IP_REMOTE} up
	atf_check -s exit:0 \
	    -o match:"inet ${IP_LOCAL}/32 -> ${IP_REMOTE}" rump.ifconfig tun0
	$DEBUG && rump.netstat -nr -f inet
	check_route_entry ${IP_REMOTE} ${IP_LOCAL} UH tun0

	export RUMP_SERVER=${SOCK_REMOTE}
	atf_check -s exit:0 rump.ifconfig tun0 create
	atf_check -s exit:0 rump.ifconfig tun0 ${IP_REMOTE} ${IP_LOCAL} up
	atf_check -s exit:0 \
	    -o match:"inet ${IP_REMOTE}/32 -> ${IP_LOCAL}" rump.ifconfig tun0
	$DEBUG && rump.netstat -nr -f inet
	check_route_entry ${IP_LOCAL} ${IP_REMOTE} UH tun0
}

tun_setup_cleanup()
{

	RUMP_SERVER=${SOCK_LOCAL} rump.halt
	RUMP_SERVER=${SOCK_REMOTE} rump.halt
}

atf_init_test_cases()
{

	atf_add_test_case tun_create_destroy
	atf_add_test_case tun_setup
}
