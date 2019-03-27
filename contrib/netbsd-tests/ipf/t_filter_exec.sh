# $NetBSD: t_filter_exec.sh,v 1.8 2013/03/17 03:00:05 jmmv Exp $
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

dotest()
{
	h_copydata $1
	infmt=$2
	outfmt=$3
	shift
	shift
	shift
	args=$@

	if [ $outfmt = hex ] ; then
		output=-x
	else
		output=
	fi

	{ while read rule; do
		atf_check -x "echo \"$rule\" | ipftest -F \
$infmt $output -Rbr - -i in $args >>out"
		echo "--------" >>out
	done; } <reg

	diff -u exp out || atf_fail "results differ"
}

mtest()
{
	h_copydata $1
	infmt=$2
	outfmt=$3
	shift
	shift
	shift
	args=$@

	if [ $outfmt = hex ] ; then
		output=-x
	else
		output=
	fi


	atf_check -o save:out ipftest -F $infmt $output -Rbr reg -i in $args
	echo "--------" >>out

	diff -u exp out || atf_fail "results differ"
}

dotest6()
{
	h_copydata $(echo ${1} | tr _ .)
	infmt=$2
	outfmt=$3
	shift
	shift
	shift
	args=$@

	if [ $outfmt = hex ] ; then
		output=-x
	else
		output=
	fi


	ipftest -6 -r /dev/null -i /dev/null >/dev/null 2>&1 \
	    || atf_skip "skipping IPv6 tests"

	{ while read rule; do
		atf_check -o save:save -x "echo \"$rule\" | \
ipftest -F $infmt $output -6br - -i in $args"
		cat save >>out
		echo "--------" >>out
	done; } <reg

	diff -u exp out || atf_fail "results differ"
}

test_case f1 dotest text text
test_case f2 dotest text text
test_case f3 dotest text text
test_case f4 dotest text text
test_case f5 dotest text text
test_case f6 dotest text text
test_case f7 dotest text text
test_case f8 dotest text text
test_case f9 dotest text text
test_case f10 dotest text text
test_case f11 dotest text text -D
test_case f12 dotest hex hex
test_case f13 dotest hex hex
test_case f14 dotest text text
test_case f15 mtest text text
test_case f16 mtest text text
test_case f17 mtest hex hex
test_case f18 mtest text text -D
test_case f19 dotest text text -T state_max=3
test_case f20 mtest text text
test_case f24 mtest hex text
test_case f25 mtest hex text -D
failing_test_case f26 dotest "Known to be broken" text text
failing_test_case f27 dotest "Known to be broken" hex text
test_case f30 dotest text text
test_case ipv6_1 dotest6 hex hex
test_case ipv6_2 dotest6 hex hex
test_case ipv6_3 dotest6 hex hex
test_case ipv6_5 dotest6 hex hex
test_case ipv6_6 dotest6 hex text

atf_init_test_cases()
{
	atf_add_test_case f1
	atf_add_test_case f2
	atf_add_test_case f3
	atf_add_test_case f4
	atf_add_test_case f5
	atf_add_test_case f6
	atf_add_test_case f7
	atf_add_test_case f8
	atf_add_test_case f9
	atf_add_test_case f10
	atf_add_test_case f11
	atf_add_test_case f12
	atf_add_test_case f13
	atf_add_test_case f14
	atf_add_test_case f15
	atf_add_test_case f16
	atf_add_test_case f17
	atf_add_test_case f18
	atf_add_test_case f19
	atf_add_test_case f20
	atf_add_test_case f24
	atf_add_test_case f25
	atf_add_test_case f26
	atf_add_test_case f27
	atf_add_test_case f30
	atf_add_test_case ipv6_1
	atf_add_test_case ipv6_2
	atf_add_test_case ipv6_3
	atf_add_test_case ipv6_5
	atf_add_test_case ipv6_6

}
