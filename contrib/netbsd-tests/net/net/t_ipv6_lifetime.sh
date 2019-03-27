#	$NetBSD: t_ipv6_lifetime.sh,v 1.6 2016/11/25 08:51:17 ozaki-r Exp $
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

SOCK=unix://sock
BUS=./bus

DEBUG=${DEBUG:-false}

deprecated="[Dd][Ee][Pp][Rr][Ee][Cc][Aa][Tt][Ee][Dd]"

atf_test_case basic cleanup

basic_head()
{
	atf_set "descr" "Tests for IPv6 address lifetime"
	atf_set "require.progs" "rump_server"
}

basic_body()
{
	local time=5
	local bonus=2
	local ip="fc00::1"

	rump_server_start $SOCK netinet6
	rump_server_add_iface $SOCK shmif0 $BUS

	export RUMP_SERVER=$SOCK

	atf_check -s exit:0 rump.ifconfig shmif0 up

	# A normal IP address doesn't contain preferred/valid lifetime
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $ip
	$DEBUG && rump.ifconfig -L shmif0
	atf_check -s exit:0 -o not-match:'pltime' rump.ifconfig -L shmif0
	atf_check -s exit:0 -o not-match:'vltime' rump.ifconfig -L shmif0
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $ip delete

	# Setting only a preferred lifetime
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $ip pltime $time
	$DEBUG && rump.ifconfig -L shmif0
	atf_check -s exit:0 -o match:'pltime' rump.ifconfig -L shmif0
	atf_check -s exit:0 -o match:'vltime infty' rump.ifconfig -L shmif0
	atf_check -s exit:0 sleep $(($time + $bonus))
	$DEBUG && rump.ifconfig -L shmif0
	# Should remain but marked as deprecated
	atf_check -s exit:0 -o match:"$ip.+$deprecated" rump.ifconfig -L shmif0
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $ip delete

	# Setting only a valid lifetime (invalid)
	atf_check -s not-exit:0 -e match:'Invalid argument' \
	    rump.ifconfig shmif0 inet6 $ip vltime $time

	# Setting both preferred and valid lifetimes (same value)
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $ip \
	    pltime $time vltime $time
	$DEBUG && rump.ifconfig -L shmif0
	atf_check -s exit:0 -o match:'pltime' rump.ifconfig -L shmif0
	atf_check -s exit:0 -o match:'vltime' rump.ifconfig -L shmif0
	atf_check -s exit:0 sleep $(($time + $bonus))
	$DEBUG && rump.ifconfig -L shmif0
	# Shouldn't remain anymore
	atf_check -s exit:0 -o not-match:"$ip" rump.ifconfig -L shmif0

	# Setting both preferred and valid lifetimes (pltime > vltime)
	atf_check -s not-exit:0 -e match:'Invalid argument' rump.ifconfig \
	    shmif0 inet6 $ip pltime $(($time * 2)) vltime $time

	# Setting both preferred and valid lifetimes (pltime < vltime)
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 $ip \
	    pltime $time vltime $((time * 2))
	$DEBUG && rump.ifconfig -L shmif0
	atf_check -s exit:0 -o match:'pltime' rump.ifconfig -L shmif0
	atf_check -s exit:0 -o match:'vltime' rump.ifconfig -L shmif0

	if sysctl machdep.cpu_brand 2>/dev/null | grep QEMU >/dev/null 2>&1
	then
		atf_check -s exit:0 rump.ifconfig shmif0 inet6 $ip delete
		atf_skip "unreliable under qemu, skip until PR kern/43997 fixed"
	fi

	atf_check -s exit:0 sleep $(($time + $bonus))
	$DEBUG && rump.ifconfig -L shmif0
	# Should remain but marked as deprecated
	atf_check -s exit:0 -o match:"$ip.+$deprecated" rump.ifconfig -L shmif0
	atf_check -s exit:0 sleep $(($time + $bonus))
	$DEBUG && rump.ifconfig -L shmif0
	# Shouldn't remain anymore
	atf_check -s exit:0 -o not-match:"$ip" rump.ifconfig -L shmif0

	rump_server_destroy_ifaces
}

basic_cleanup()
{
	$DEBUG && dump
	cleanup
}

atf_init_test_cases()
{
	atf_add_test_case basic
}
