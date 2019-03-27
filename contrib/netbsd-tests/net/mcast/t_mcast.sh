#	$NetBSD: t_mcast.sh,v 1.4 2016/11/25 08:51:16 ozaki-r Exp $
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

export RUMP_SERVER=unix://commsock

DEBUG=${DEBUG:-false}

run_test()
{
	local name="$1"
	local opts="$2"
	local mcast="$(atf_get_srcdir)/mcast"

	rump_server_start $RUMP_SERVER netinet6
	rump_server_add_iface $RUMP_SERVER shmif0 bus1
	atf_check -s exit:0 rump.ifconfig shmif0 10.0.0.2/24
	atf_check -s exit:0 rump.ifconfig shmif0 inet6 fc00::2/64
	atf_check -s exit:0 rump.ifconfig shmif0 up

	atf_check -s exit:0 rump.ifconfig -w 10
	atf_check -s not-exit:0 -x "rump.ifconfig shmif0 |grep -q tentative"

	# A route to the mcast address is required to join the mcast group
	atf_check -s exit:0 -o ignore rump.route add default 10.0.0.1
	atf_check -s exit:0 -o ignore rump.route add -inet6 default fc00::1

	$DEBUG && rump.ifconfig
	$DEBUG && rump.netstat -nr

	export LD_PRELOAD=/usr/lib/librumphijack.so
	#$DEBUG && /usr/sbin/ifmcstat  # Not yet run on rump kernel
	if $DEBUG; then
		atf_check -s exit:0 -o ignore $mcast -d ${opts}
	else
		atf_check -s exit:0 $mcast ${opts}
	fi
	#$DEBUG && /usr/sbin/ifmcstat  # Not yet run on rump kernel
	unset LD_PRELOAD
}

add_test()
{
	local name=$1
	local opts="$2"
	local desc="$3"

	atf_test_case "mcast_${name}" cleanup
	eval "mcast_${name}_head() { \
			atf_set \"descr\" \"${desc}\"; \
			atf_set \"require.progs\" \"rump_server\"; \
		}; \
	    mcast_${name}_body() { \
			run_test \"${name}\" \"${opts}\"; \
			rump_server_destroy_ifaces; \
		}; \
	    mcast_${name}_cleanup() { \
			${DEBUG} && dump; \
			cleanup; \
		}"
	atf_add_test_case "mcast_${name}"
}

atf_init_test_cases()
{

	add_test conninet4            "-c -4" \
	    "Checks connected multicast for ipv4"
	add_test connmappedinet4      "-c -m -4" \
	    "Checks connected multicast for mapped ipv4"
	add_test connmappedbuginet4   "-c -m -b -4" \
	    "Checks connected multicast for mapped ipv4 using the v4 ioctls"
	add_test conninet6            "-c -6" \
	    "Checks connected multicast for ipv6"
	add_test unconninet4          "-4" \
	    "Checks unconnected multicast for ipv4"
	add_test unconnmappedinet4    "-m -4" \
	    "Checks unconnected multicast for mapped ipv4"
	add_test unconnmappedbuginet4 "-m -b -4" \
	    "Checks unconnected multicast for mapped ipv4 using the v4 ioctls"
	add_test unconninet6          "-6" \
	    "Checks unconnected multicast for ipv6"
}
