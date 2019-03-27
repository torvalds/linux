#	$NetBSD: t_basic.sh,v 1.8 2013/04/07 19:14:03 christos Exp $
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

unpack_file()
{

	atf_check -s exit:0 uudecode $(atf_get_srcdir)/${1}.bz2.uue
	atf_check -s exit:0 bunzip2 -f ${1}.bz2
}

test_case()
{
	local name="${1}"; shift

	atf_test_case "${name}"
	eval "${name}_head() {  }"
	eval "${name}_body() { \
		unpack_file shmbus ; \
		${name} ; \
	}"
}

test_case header
test_case contents
test_case pcap

ehdr='bus version 2, lock: 0, generation: 22, firstoff: 0x9e414, lastoff: 0x9dfd0'

header()
{

	atf_check -s exit:0 -o inline:"${ehdr}\n" shmif_dumpbus -h shmbus
}

contents()
{

	unpack_file d_pkthdrs.out
	atf_check -s exit:0 -o file:d_pkthdrs.out \
	    shmif_dumpbus shmbus
}

pcap()
{

	unpack_file d_pcap.out
	atf_check -s exit:0 -o ignore shmif_dumpbus -p pcap shmbus
#
#	should not fail anymore...
#
#	Used to fail for "PR bin/44721"
	atf_check -s exit:0 -o file:d_pcap.out -e ignore \
	    tcpdump -tt -r pcap
}

atf_init_test_cases()
{

	atf_add_test_case header
	atf_add_test_case contents
	atf_add_test_case pcap
}
