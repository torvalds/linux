# $NetBSD: t_nat_exec.sh,v 1.22 2015/12/26 08:01:58 pgoyette Exp $
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

nattest()
{
	h_copydata $1
	infmt=$2
	outfmt=$3
	shift
	shift
	shift
	args=$@

	if [ $outfmt = hex ] ; then
		format="-xF $infmt"
	else
		format="-F $infmt"
	fi

	format="$format"

	test -f in  && test -f reg || atf_fail "Test input file missing"

	{ while read rule; do
		atf_check -o save:save -x \
		    "echo \"$rule\" | ipftest $format -RDbN - -i in $args"
		cat save >>out
		echo "-------------------------------" >>out
	done; } <reg

	diff -u exp out || atf_fail "results differ"
}

test_case n1 nattest text text
test_case n2 nattest text text
test_case n3 nattest text text
test_case n4 nattest text text
test_case n5 nattest text text
test_case n6 nattest text text
test_case n7 nattest text text
test_case n8 nattest hex hex -T update_ipid=0
test_case n9 nattest hex hex -T update_ipid=0
test_case n10 nattest hex hex -T update_ipid=0
test_case n11 nattest text text
test_case n12 nattest hex hex -T update_ipid=0
test_case n13 nattest text text
test_case n14 nattest text text
test_case n15 nattest text text -T update_ipid=0
test_case n16 nattest hex hex -D
test_case n17 nattest hex hex -D
test_case n100 nattest text text
test_case n101 nattest text text
test_case n102 nattest text text
test_case n103 nattest text text
test_case n104 nattest hex hex -T update_ipid=0
test_case n105 nattest hex hex -T update_ipid=0
test_case n106 nattest hex hex -T update_ipid=0
test_case n200 nattest hex hex -T update_ipid=0
test_case n1_6 nattest text text -6
test_case n2_6 nattest text text -6
#test_case n3_6 nattest text text -6
test_case n4_6 nattest text text -6
test_case n5_6 nattest text text -6
test_case n6_6 nattest text text -6
test_case n7_6 nattest text text -6
failing_test_case_be n8_6 nattest "See PR kern/47665" hex hex -6
failing_test_case_be n9_6 nattest "See PR kern/47665" hex hex -6
test_case n11_6 nattest text text -6
test_case n12_6 nattest hex hex -6
test_case n15_6 nattest text text -6

atf_init_test_cases()
{
	atf_add_test_case n1
	atf_add_test_case n2
	atf_add_test_case n3
	atf_add_test_case n4
	atf_add_test_case n5
	atf_add_test_case n6
	atf_add_test_case n7
	atf_add_test_case n8
	atf_add_test_case n9
	atf_add_test_case n10
	atf_add_test_case n11
	atf_add_test_case n12
	atf_add_test_case n13
	atf_add_test_case n14
	atf_add_test_case n16
	atf_add_test_case n17
	atf_add_test_case n100
	atf_add_test_case n101
	atf_add_test_case n102
	atf_add_test_case n103
	atf_add_test_case n104
	atf_add_test_case n105
	atf_add_test_case n106
	atf_add_test_case n200

	atf_add_test_case n1_6
	atf_add_test_case n2_6
#	atf_add_test_case n3_6
	atf_add_test_case n4_6
	atf_add_test_case n5_6
	atf_add_test_case n6_6
	atf_add_test_case n7_6
	atf_add_test_case n8_6
	atf_add_test_case n9_6
	atf_add_test_case n11_6
	atf_add_test_case n12_6
	atf_add_test_case n15_6
}
