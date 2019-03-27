# $NetBSD: t_ifconf.sh,v 1.3 2016/08/10 22:30:02 kre Exp $
#
# Copyright (c) 2014 The NetBSD Foundation, Inc.
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

RUMP_FLAGS=\
"-lrumpnet -lrumpnet_net -lrumpnet_netinet -lrumpnet_shmif -lrumpdev"

atf_test_case basic cleanup
basic_head()
{

	atf_set "descr" "basic ifconf (SIOCGIFCONF) test"
	atf_set "require.progs" "rump_server"
}

basic_body()
{
	local ifconf="$(atf_get_srcdir)/ifconf"

	atf_check -s exit:0 rump_server ${RUMP_FLAGS} ${RUMP_SERVER1}

	export RUMP_SERVER=${RUMP_SERVER1}
	export LD_PRELOAD=/usr/lib/librumphijack.so

	# lo0 (127.0.0.1 and link local)
	atf_check -s exit:0 -o match:'^2$' "$ifconf" total
	atf_check -s exit:0 -o match:'lo0' "$ifconf" list

	# Add shmif0 (no address)
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 -o match:'^3$' "$ifconf" total
	atf_check -s exit:0 -o match:'shmif0' "$ifconf" list

	# Add shmif1 (no address)
	atf_check -s exit:0 rump.ifconfig shmif1 create
	atf_check -s exit:0 -o match:'^4$' "$ifconf" total
	atf_check -s exit:0 -o match:'shmif1' "$ifconf" list

	# Add an address to shmif0
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr shmbus
	atf_check -s exit:0 rump.ifconfig shmif0 192.168.0.1/24
	atf_check -s exit:0 -o match:'^5$' "$ifconf" total

	# Vary the number of requesting interfaces
	atf_check -s exit:0 -o match:1 -x "$ifconf list 1 | wc -l"
	atf_check -s exit:0 -o match:2 -x "$ifconf list 2 | wc -l"
	atf_check -s exit:0 -o match:3 -x "$ifconf list 3 | wc -l"
	atf_check -s exit:0 -o match:4 -x "$ifconf list 4 | wc -l"
	atf_check -s exit:0 -o match:5 -x "$ifconf list 5 | wc -l"
	atf_check -s exit:0 -o match:5 -x "$ifconf list 6 | wc -l"

	# Check if removing an interface is reflected
	atf_check -s exit:0 rump.ifconfig shmif0 destroy
	atf_check -s exit:0 -o match:'^3$' "$ifconf" total
	atf_check -s exit:0 -o not-match:'shmif0' "$ifconf" list
	atf_check -s exit:0 -o match:1 -x "$ifconf list 1 | wc -l"
	atf_check -s exit:0 -o match:2 -x "$ifconf list 2 | wc -l"
	atf_check -s exit:0 -o match:3 -x "$ifconf list 3 | wc -l"
	atf_check -s exit:0 -o match:3 -x "$ifconf list 4 | wc -l"

	unset LD_PRELOAD
	unset RUMP_SERVER
}

basic_cleanup()
{

	RUMP_SERVER=${RUMP_SERVER1} rump.halt
}

atf_init_test_cases()
{

	atf_add_test_case basic
}
