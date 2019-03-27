#	$NetBSD: t_script.sh,v 1.7 2014/11/16 04:47:18 uebayasi Exp $
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

atf_test_case order_default
order_default_head() {
	atf_set "descr" "check if default object ordering works"
	atf_set "require.progs" "cc" "ld" "readelf" "nm" "sed" "grep"
}

order_default_body() {
	cat > test.x << EOF
SECTIONS {
	/* do nothing; but ld has implicit scripts internally */
	/* which usually do: *(.data) *(.data.*) */
}
EOF
	order_assert_descending
}

################################################################################

atf_test_case order_merge
order_merge_head() {
	atf_set "descr" "check if glob merge keeps object ordering"
	atf_set "require.progs" ${order_require_progs}
}

order_merge_body() {
	cat > test.x << EOF
SECTIONS {
	.data : {
		*(.data .data.*)
	}
}
EOF
	order_assert_descending
}

################################################################################

atf_test_case order_reorder
order_reorder_head() {
	atf_set "descr" "check if object reordering works"
	atf_set "require.progs" ${order_require_progs}
}

order_reorder_body() {
	cat > test.x << EOF
SECTIONS {
	.data : {
		*(.data)
		*(.data.a)
		*(.data.b)
		*(.data.c)
	}
}
EOF
	order_assert_ascending
}

################################################################################

atf_test_case order_sort
order_sort_head() {
	atf_set "descr" "check if object sort works"
	atf_set "require.progs" ${order_require_progs}
}

order_sort_body() {
	cat > test.x << EOF
SECTIONS {
	.data : {
		*(.data)
		/* SORT_BY_NAME */
		SORT(*)(.data.*)
	}
}
EOF
	order_assert_ascending
}

################################################################################

atf_test_case multisec
multisec_head() {
	atf_set "descr" "check if multiple SECTIONS commands work"
	atf_set "require.progs" ${order_require_progs}
}

multisec_body() {
	cat > test.c << EOF
#include <sys/cdefs.h>
char a __section(".data.a") = 'a';
char b __section(".data.b") = 'b';
char c __section(".data.c") = 'c';
EOF
	atf_check -s exit:0 -o ignore -e ignore cc -c test.c

	cat > test.x << EOF
SECTIONS {
	.data : {
		*(.data)
		*(.data.a)
	}
}
SECTIONS {
	.data : {
		*(.data)
		*(.data.b)
	}
}
EOF
	atf_check -s exit:0 -o ignore -e ignore \
	    ld -r -T test.x -Map test.map -o test.ro test.o
	extract_section_names test.ro >test.secs
	extract_symbol_names test.ro >test.syms
	assert_nosec '\.data\.a'
	assert_nosec '\.data\.b'
	assert_sec '\.data\.c'
}

################################################################################

order_require_progs="cc ld readelf nm sed grep"

order_assert_ascending() {
	order_assert_order a b c
}

order_assert_descending() {
	order_assert_order c b a
}

order_assert_order() {
	order_compile
	order_link
	{
		match $1 && match $2 && match $3
	} <test.syms
	atf_check test "$?" -eq 0
}

order_compile() {
	for i in a b c; do
		cat > $i.c << EOF
#include <sys/cdefs.h>
char $i __section(".data.$i") = '$i';
EOF
		atf_check -s exit:0 -o ignore -e ignore cc -c $i.c
	done
	cat > test.c << EOF
int main(void) { return 0; }
EOF
	atf_check -s exit:0 -o ignore -e ignore cc -c test.c
}

order_link() {
	# c -> b -> a
	atf_check -s exit:0 -o ignore -e ignore \
	    ld -r -T test.x -Map test.map -o x.o c.o b.o a.o
	atf_check -s exit:0 -o ignore -e ignore \
	    cc -o test test.o x.o
	extract_symbol_names test |
	grep '^[abc]$' >test.syms
}

extract_section_names() {
	readelf -S "$1" |
	sed -ne '/\] \./ { s/^.*\] //; s/ .*$//; p }'
}

extract_symbol_names() {
	nm -n "$1" |
	sed -e 's/^.* //'
}

match() {
	read line
	case "$line" in
	*"$1"*) return 0;
	esac
	return 1
}

assert_sec() {
	atf_check -s exit:0 -o ignore -e ignore \
	    grep "^$1\$" test.secs
}

assert_nosec() {
	atf_check -s exit:1 -o ignore -e ignore \
	    grep "^$1\$" test.secs
}

################################################################################

atf_init_test_cases()
{
	atf_add_test_case order_default
	atf_add_test_case order_merge
	atf_add_test_case order_reorder
	atf_add_test_case order_sort
	atf_add_test_case multisec
}
