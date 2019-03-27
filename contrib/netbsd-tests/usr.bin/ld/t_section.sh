#	$NetBSD: t_section.sh,v 1.4 2015/02/17 11:51:04 martin Exp $
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

################################################################################

atf_test_case startstop
startstop_head() {
	atf_set "descr" "check if __start_*/__stop_* symbols are generated"
	atf_set "require.progs" "cc"
}

startstop_body() {
	cat > test.c << EOF
#include <sys/cdefs.h>
int i __section("hoge");
extern int __start_hoge[], __stop_hoge[];
int main(void) { return __start_hoge[0] + __stop_hoge[0]; }
EOF
	atf_check -s exit:0 -o ignore -e ignore cc -o test test.c
}

################################################################################

atf_test_case orphan
orphan_head() {
	atf_set "descr" "check orphan section placement"
	atf_set "require.progs" "cc" "readelf" "grep"
}

orphan_body() {
	cat > test.c << EOF
#include <sys/cdefs.h>
/* read-only orphan */
const char a[] __section("hoge") = "hoge";
/* read-write orphan */
char b[] __section("fuga") = { 'f', 'u', 'g', 'a', '\0' };
/* .data */
int c[1024] = { 123, 20, 1, 0 };
/* .bss */
int d = 0;
/* .text */
int main(void) { return 0; }
EOF
	atf_check -s exit:0 -o ignore -e ignore cc -o test test.c
	readelf -S test |
	grep ' \.text\| hoge\| \.data\| fuga\| \.bss' >test.secs
	{
		# Read-only orphan sections are placed after well-known
		# read-only sections (.text, .rodata) but before .data.
		match ".text" &&
		match "hoge" &&
		# Read-write orphan sections are placed after well-known
		# read-write sections (.data) but before .bss.
		match ".data" &&
		match "fuga" &&
		match ".bss" &&
		:
	} < test.secs
	atf_check test "$?" -eq 0
}

match() {
	read line
	case "$line" in
	*"$1"*) return 0;
	esac
	return 1
}

################################################################################

atf_init_test_cases()
{
	atf_add_test_case startstop
	atf_add_test_case orphan
}
