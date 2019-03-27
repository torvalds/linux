# $NetBSD: t_integration.sh,v 1.4 2014/04/21 19:10:41 christos Exp $
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

LINT1=/usr/libexec/lint1

Names=

check_valid()
{
	atf_check -s exit:0 ${LINT1} -g -S "$(atf_get_srcdir)/$1" /dev/null
}

check_invalid()
{
	atf_check -s not-exit:0 -o ignore -e ignore ${LINT1} -g -S -w \
	    "$(atf_get_srcdir)/$1" /dev/null
}

test_case()
{
	local result="${1}"; shift
	local name="${1}"; shift
	local descr="${*}"

	atf_test_case ${name}
	eval "${name}_head() {
		atf_set \"descr\" \"${descr}\";
		atf_set \"require.progs\" \"${LINT1}\";
	}"
	eval "${name}_body() {
		${result} d_${name}.c;
	}"

	Names="${Names} ${name}"
}

test_case check_valid c99_struct_init "Checks C99 struct initialization"
test_case check_valid c99_union_init1 "Checks C99 union initialization"
test_case check_valid c99_union_init2 "Checks C99 union initialization"
test_case check_valid c99_union_init3 "Checks C99 union initialization"
test_case check_valid c99_recursive_init "Checks C99 recursive struct/union" \
    "initialization"
test_case check_valid c9x_recursive_init "Checks C9X struct/union member" \
    "init, with nested union and trailing member"
test_case check_valid nested_structs "Checks nested structs"
test_case check_valid packed_structs "Checks packed structs"

test_case check_valid cast_init "Checks cast initialization"
test_case check_valid cast_init2 "Checks cast initialization as the rhs of a" \
    "- operand"
test_case check_valid cast_lhs "Checks whether pointer casts are valid lhs" \
    "lvalues"

test_case check_valid gcc_func "Checks GCC __FUNCTION__"
test_case check_valid c99_func "Checks C99 __func__"

test_case check_valid gcc_variable_array_init "Checks GCC variable array" \
    "initializers"
test_case check_valid c9x_array_init "Checks C9X array initializers"
test_case check_valid c99_decls_after_stmt "Checks C99 decls after statements"
test_case check_valid c99_decls_after_stmt3 "Checks C99 decls after statements"
test_case check_valid nolimit_init "Checks no limit initializers"
test_case check_valid zero_sized_arrays "Checks zero sized arrays"

test_case check_valid compound_literals1 "Checks compound literals"
test_case check_valid compound_literals2 "Checks compound literals"
test_case check_valid gcc_compound_statements1 "Checks GCC compound statements"
test_case check_valid gcc_compound_statements2 "Checks GCC compound" \
    "statements with non-expressions"
test_case check_valid gcc_compound_statements3 "Checks GCC compound" \
    "statements with void type"
# XXX: Because of polymorphic __builtin_isnan and expression has null effect
# test_case check_valid gcc_extension "Checks GCC __extension__ and __typeof__"

test_case check_valid cvt_in_ternary "Checks CVT nodes handling in ?" \
test_case check_valid cvt_constant "Checks constant conversion"
test_case check_valid ellipsis_in_switch "Checks ellipsis in switch()"
test_case check_valid c99_complex_num "Checks C99 complex numbers"
test_case check_valid c99_complex_split "Checks C99 complex access"
test_case check_valid c99_for_loops "Checks C99 for loops"
test_case check_valid alignof "Checks __alignof__"
test_case check_valid shift_to_narrower_type "Checks that type shifts that" \
    "result in narrower types do not produce warnings"

test_case check_invalid constant_conv1 "Checks failing on information-losing" \
    "constant conversion in argument lists"
test_case check_invalid constant_conv2 "Checks failing on information-losing" \
    "constant conversion in argument lists"

test_case check_invalid type_conv1 "Checks failing on information-losing" \
    "type conversion in argument lists"
test_case check_invalid type_conv2 "Checks failing on information-losing" \
    "type conversion in argument lists"
test_case check_invalid type_conv3 "Checks failing on information-losing" \
    "type conversion in argument lists"

test_case check_invalid incorrect_array_size "Checks failing on incorrect" \
    "array sizes"

test_case check_invalid long_double_int "Checks for confusion of 'long" \
    "double' with 'long int'; PR 39639"

atf_init_test_cases()
{
	for name in ${Names}; do
		atf_add_test_case ${name}
	done
}
