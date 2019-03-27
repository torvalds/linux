#	$NetBSD: t_basic.sh,v 1.1 2017/01/16 08:18:11 ozaki-r Exp $
#
# Copyright (c) 2017 Internet Initiative Japan Inc.
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

SOCK_CLIENT=unix://carp_client
SOCK_MASTER=unix://carp_master
SOCK_BACKUP=unix://carp_backup
BUS=bus_carp
IP_CLIENT=10.1.1.240
IP_MASTER=10.1.1.1
IP_BACKUP=10.1.1.2
IP_CARP=10.1.1.100
TIMEOUT=3

atf_test_case carp_handover cleanup

carp_handover_head()
{

	atf_set "descr" "Tests for CARP handover"
	atf_set "require.progs" "rump_server"
}

setup_carp()
{
	local sock=$1
	local master=$2
	local carpif= ip= advskew=

	if $master; then
		carpif=carp0
		ip=$IP_MASTER
		advskew=0
	else
		carpif=carp1
		ip=$IP_BACKUP
		advskew=200
	fi

	export RUMP_SERVER=$sock
	atf_check -s exit:0 rump.ifconfig $carpif create
	atf_check -s exit:0 rump.ifconfig shmif0 $ip/24 up
	atf_check -s exit:0 rump.ifconfig $carpif \
	    vhid 175 advskew $advskew advbase 1 pass s3cret \
	    $IP_CARP netmask 255.255.255.0
	atf_check -s exit:0 rump.ifconfig -w 10
}

wait_handover()
{
	local i=0

	export RUMP_SERVER=$SOCK_CLIENT

	while [ $i -ne 5 ]; do
		$DEBUG && echo "Trying ping $IP_CARP"
		rump.ping -n -w 1 -c 1 $IP_CARP >/dev/null
		if [ $? = 0 ]; then
			$DEBUG && echo "Passed ping $IP_CARP"
			break;
		fi
		$DEBUG && echo "Failed ping $IP_CARP"
		i=$((i + 1))
	done

	if [ $i -eq 5 ]; then
		atf_fail "Failed to failover (5 sec)"
	fi
}

carp_handover_body()
{

	rump_server_start $SOCK_CLIENT
	rump_server_start $SOCK_MASTER
	rump_server_start $SOCK_BACKUP

	rump_server_add_iface $SOCK_CLIENT shmif0 $BUS
	rump_server_add_iface $SOCK_MASTER shmif0 $BUS
	rump_server_add_iface $SOCK_BACKUP shmif0 $BUS

	setup_carp $SOCK_MASTER true
	setup_carp $SOCK_BACKUP false

	export RUMP_SERVER=$SOCK_CLIENT
	atf_check -s exit:0 rump.ifconfig shmif0 $IP_CLIENT/24 up
	atf_check -s exit:0 rump.ifconfig -w 10

	# Check that the primary addresses are up
	atf_check -s exit:0 -o ignore \
	    rump.ping -n -w $TIMEOUT -c 1 $IP_MASTER
	atf_check -s exit:0 -o ignore \
	    rump.ping -n -w $TIMEOUT -c 1 $IP_BACKUP

	# Give carp a while to croak
	sleep 4

	# Check state
	export RUMP_SERVER=$SOCK_MASTER
	$DEBUG && rump.ifconfig
	atf_check -s exit:0 -o match:'carp: MASTER carpdev shmif0' \
	    rump.ifconfig carp0
	export RUMP_SERVER=$SOCK_BACKUP
	$DEBUG && rump.ifconfig
	atf_check -s exit:0 -o match:'carp: BACKUP carpdev shmif0' \
	    rump.ifconfig carp1
	export RUMP_SERVER=$SOCK_CLIENT

	# Check that the shared IP works
	atf_check -s exit:0 -o ignore \
	    rump.ping -n -w $TIMEOUT -c 1 $IP_CARP

	# KILLING SPREE
	env RUMP_SERVER=$SOCK_MASTER rump.halt
	sleep 1

	# Check that primary is now dead
	atf_check -s not-exit:0 -o ignore \
	    rump.ping -n -w $TIMEOUT -c 1 $IP_MASTER

	# Do it in installments. carp will cluck meanwhile
	wait_handover

	# Check state
	export RUMP_SERVER=$SOCK_BACKUP
	$DEBUG && rump.ifconfig
	atf_check -s exit:0 -o match:'carp: MASTER carpdev shmif0' \
	    rump.ifconfig carp1
}

carp_handover_cleanup()
{

	$DEBUG && dump
	cleanup
}

atf_init_test_cases()
{

	atf_add_test_case carp_handover
}
