# $NetBSD: t_nat_ipf_exec.sh,v 1.7 2012/07/07 23:29:44 pgoyette Exp $
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

natipftest()
{
	h_copydata $1
	cp $(atf_get_srcdir)/regress/$1.nat nat
	cp $(atf_get_srcdir)/regress/$1.ipf ipf
	many=$2
	infmt=$3
	outfmt=$4
	shift
	shift
	shift
	shift
	args=$@

	if [ $outfmt = hex ] ; then
		format="-xF $infmt"
	else
		format="-F $infmt"
	fi

	case $many in
	single)
		{ while read rule; do
			atf_check -o save:save -x "echo \"$rule\" | \
ipftest -R $format -b -r ipf -N - -i in $args"
			cat save >>out
			echo "-------------------------------" >>out
		done; } <nat
		;;
	multi)
		atf_check -o save:out ipftest -R $args \
			$format -b -r ipf -N nat -i in
		echo "-------------------------------" >>out
		;;
	esac

	diff -u exp out || atf_fail "results differ"
}

test_case ni1 natipftest multi hex hex -T update_ipid=1
test_case ni2 natipftest single hex hex -T update_ipid=1
test_case ni3 natipftest single hex hex -T update_ipid=1
test_case ni4 natipftest single hex hex -T update_ipid=1
test_case ni5 natipftest single hex hex -T update_ipid=1
test_case ni6 natipftest multi hex text -T update_ipid=1 -D
test_case ni7 natipftest single hex hex -T update_ipid=1
test_case ni8 natipftest single hex hex -T update_ipid=1
test_case ni9 natipftest single hex hex -T update_ipid=1
test_case ni10 natipftest single hex hex -T update_ipid=1
test_case ni11 natipftest single hex hex -T update_ipid=1
test_case ni12 natipftest single hex hex -T update_ipid=1
test_case ni13 natipftest single hex hex -T update_ipid=1
test_case ni14 natipftest single hex hex -T update_ipid=1
test_case ni15 natipftest single hex hex -T update_ipid=1
test_case ni16 natipftest single hex hex -T update_ipid=1
test_case ni17 natipftest multi text text
test_case ni18 natipftest multi text text
test_case ni19 natipftest single hex hex -T update_ipid=0
test_case ni20 natipftest single hex hex -T update_ipid=0 -D
test_case ni21 natipftest multi text text
test_case ni23 natipftest multi text text -D

atf_init_test_cases()
{
	atf_add_test_case ni1
	atf_add_test_case ni2
	atf_add_test_case ni3
	atf_add_test_case ni4
	atf_add_test_case ni5
	atf_add_test_case ni6
	atf_add_test_case ni7
	atf_add_test_case ni8
	atf_add_test_case ni9
	atf_add_test_case ni10
	atf_add_test_case ni11
	atf_add_test_case ni12
	atf_add_test_case ni13
	atf_add_test_case ni14
	atf_add_test_case ni15
	atf_add_test_case ni16
	atf_add_test_case ni17
	atf_add_test_case ni18
	atf_add_test_case ni19
	atf_add_test_case ni20
	atf_add_test_case ni21
	atf_add_test_case ni23

}
