# $NetBSD: t_bpf.sh,v 1.3 2012/11/29 16:05:34 pgoyette Exp $
#
# Copyright (c) 2008, 2010 The NetBSD Foundation, Inc.
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
#
# (C)opyright 1993-1996 by Darren Reed.
#
# See the IPFILTER.LICENCE file for details on licencing.
#

itest()
{
	h_copydata $1

	case $3 in
	ipf)
		atf_check -o file:exp -e ignore ipf -Rnvf reg
		;;
	ipftest)
		atf_check -o file:exp ipftest -D -r reg -i /dev/null
		;;
	esac
}

bpftest()
{
	h_copydata $(echo ${1} | tr _ -)
	cp "$(atf_get_srcdir)/input/$(echo ${1} | sed s,bpf_,,)" in

	{ while read rule; do
		atf_check -o save:save -x "echo '$rule' | ipftest -Rbr - -i in"
		cat save >>out
		echo "--------" >>out
	done; } <reg

	diff -u exp out || atf_fail "results differ"
}

broken_test_case bpf1 itest text ipf
broken_test_case bpf_f1 bpftest text text

atf_init_test_cases()
{
	atf_add_test_case bpf1
	atf_add_test_case bpf_f1
}
