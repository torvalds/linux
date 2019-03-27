#	$NetBSD: t_traceroute.sh,v 1.6 2016/08/10 23:17:35 kre Exp $
#
# Copyright (c) 2010 The NetBSD Foundation, Inc.
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
"rump_server -lrumpnet -lrumpnet_net -lrumpnet_netinet -lrumpnet_shmif -lrumpdev"

atf_test_case basic cleanup
basic_head()
{

	atf_set "descr" "Does a simple three-hop traceroute"
	atf_set "require.progs" "rump_server"
}

cfgendpt ()
{

	sock=${1}
	addr=${2}
	route=${3}
	bus=${4}

	export RUMP_SERVER=${sock}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr ${bus}
	atf_check -s exit:0 rump.ifconfig shmif0 inet ${addr} netmask 0xffffff00
	atf_check -s exit:0 -o ignore rump.route add default ${route}
}

threeservers()
{

	atf_check -s exit:0 ${netserver} unix://commsock1
	atf_check -s exit:0 ${netserver} unix://commsock2
	atf_check -s exit:0 ${netserver} unix://commsock3

	# configure endpoints
	cfgendpt unix://commsock1 1.2.3.4 1.2.3.1 bus1
	cfgendpt unix://commsock3 2.3.4.5 2.3.4.1 bus2

	# configure the router
	export RUMP_SERVER=unix://commsock2
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr bus1
	atf_check -s exit:0 rump.ifconfig shmif0 inet 1.2.3.1 netmask 0xffffff00

	atf_check -s exit:0 rump.ifconfig shmif1 create
	atf_check -s exit:0 rump.ifconfig shmif1 linkstr bus2
	atf_check -s exit:0 rump.ifconfig shmif1 inet 2.3.4.1 netmask 0xffffff00
}

threecleanup()
{
	env RUMP_SERVER=unix://commsock1 rump.halt
	env RUMP_SERVER=unix://commsock2 rump.halt
	env RUMP_SERVER=unix://commsock3 rump.halt
}

threetests()
{

	threeservers
	export RUMP_SERVER=unix://commsock1
	atf_check -s exit:0 -o inline:'1.2.3.1\n2.3.4.5\n' -e ignore -x	\
	    "rump.traceroute ${1} -n 2.3.4.5 | awk '{print \$2}'"
	export RUMP_SERVER=unix://commsock3
	atf_check -s exit:0 -o inline:'2.3.4.1\n1.2.3.4\n' -e ignore -x \
	    "rump.traceroute ${1} -n 1.2.3.4 | awk '{print \$2}'"
}

basic_body()
{
	threetests
}

basic_cleanup()
{
	threecleanup
}

atf_test_case basic_icmp cleanup
basic_icmp_head()
{

	atf_set "descr" "Does an ICMP-based three-hop traceroute"
	atf_set "require.progs" "rump_server"
}

basic_icmp_body()
{
	threetests -I
}

basic_icmp_cleanup()
{
	threecleanup
}

atf_init_test_cases()
{

	atf_add_test_case basic
	atf_add_test_case basic_icmp
}
