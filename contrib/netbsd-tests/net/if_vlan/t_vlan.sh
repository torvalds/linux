#	$NetBSD: t_vlan.sh,v 1.1 2016/11/26 03:19:49 ozaki-r Exp $
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

BUS=bus
SOCK_LOCAL=unix://commsock1
SOCK_REMOTE=unix://commsock2
IP_LOCAL=10.0.0.1
IP_REMOTE=10.0.0.2

DEBUG=${DEBUG:-false}

atf_test_case vlan_create_destroy cleanup
vlan_create_destroy_head()
{

	atf_set "descr" "tests of creation and deletion of vlan interface"
	atf_set "require.progs" "rump_server"
}

vlan_create_destroy_body()
{

	rump_server_start $SOCK_LOCAL vlan

	export RUMP_SERVER=${SOCK_LOCAL}

	atf_check -s exit:0 rump.ifconfig vlan0 create
	atf_check -s exit:0 rump.ifconfig vlan0 up
	atf_check -s exit:0 rump.ifconfig vlan0 down
	atf_check -s exit:0 rump.ifconfig vlan0 destroy
}

vlan_create_destroy_cleanup()
{

	$DEBUG && dump
	cleanup
}

atf_test_case vlan_basic cleanup
vlan_basic_head()
{

	atf_set "descr" "tests of communications over vlan interfaces"
	atf_set "require.progs" "rump_server"
}

vlan_basic_body()
{

	rump_server_start $SOCK_LOCAL vlan
	rump_server_add_iface $SOCK_LOCAL shmif0 $BUS
	rump_server_start $SOCK_REMOTE vlan
	rump_server_add_iface $SOCK_REMOTE shmif0 $BUS

	export RUMP_SERVER=$SOCK_LOCAL
	atf_check -s exit:0 rump.ifconfig shmif0 up
	export RUMP_SERVER=$SOCK_REMOTE
	atf_check -s exit:0 rump.ifconfig shmif0 up

	export RUMP_SERVER=$SOCK_LOCAL
	atf_check -s exit:0 rump.ifconfig vlan0 create
	atf_check -s exit:0 rump.ifconfig vlan0 vlan 10 vlanif shmif0
	atf_check -s exit:0 rump.ifconfig vlan0 $IP_LOCAL/24
	atf_check -s exit:0 rump.ifconfig vlan0 up
	atf_check -s exit:0 rump.ifconfig -w 10

	export RUMP_SERVER=$SOCK_REMOTE
	atf_check -s exit:0 rump.ifconfig vlan0 create
	atf_check -s exit:0 rump.ifconfig vlan0 vlan 10 vlanif shmif0
	atf_check -s exit:0 rump.ifconfig vlan0 $IP_REMOTE/24
	atf_check -s exit:0 rump.ifconfig vlan0 up
	atf_check -s exit:0 rump.ifconfig -w 10

	export RUMP_SERVER=$SOCK_LOCAL
	atf_check -s exit:0 -o ignore rump.ping -n -w 1 -c 1 $IP_REMOTE
}

vlan_basic_cleanup()
{

	$DEBUG && dump
	cleanup
}

atf_init_test_cases()
{

	atf_add_test_case vlan_create_destroy
	atf_add_test_case vlan_basic
}
