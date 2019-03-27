#	$NetBSD: t_shmif.sh,v 1.3 2016/08/10 23:49:03 kre Exp $
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

atf_test_case crossping cleanup

NKERN=8

crossping_head()
{
	atf_set "descr" "run $NKERN rump kernels on one shmif bus and crossping"
}

startserver()
{

	export RUMP_SERVER=unix://sock${1}
	atf_check -s exit:0 rump_server -lrumpnet -lrumpnet_net \
	    -lrumpnet_netinet -lrumpnet_shmif -lrumpdev ${RUMP_SERVER}
	atf_check -s exit:0 rump.ifconfig shmif0 create
	atf_check -s exit:0 rump.ifconfig shmif0 linkstr shmbus
	atf_check -s exit:0 rump.ifconfig shmif0 inet 1.1.1.${1}
}

pingothers()
{

}

crossping_body()
{

	for x in `jot ${NKERN}` ; do startserver $x ; done
	for x in `jot ${NKERN}`
	do
		export RUMP_SERVER=unix://sock${x}
		for y in `jot ${NKERN}`
		do
			[ ${y} -eq ${x} ] && continue
			atf_check -s exit:0 -o ignore -e ignore \
			    rump.ping -c 1 1.1.1.${y}
		done
	done
}

crossping_cleanup()
{

	for x in `jot ${NKERN}` ; do RUMP_SERVER=unix://sock${x} rump.halt ;done
	:
}

atf_init_test_cases()
{

	atf_add_test_case crossping
}
