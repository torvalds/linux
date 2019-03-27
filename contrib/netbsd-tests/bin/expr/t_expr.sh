# $NetBSD: t_expr.sh,v 1.3 2012/03/27 07:23:06 jruoho Exp $
#
# Copyright (c) 2007 The NetBSD Foundation, Inc.
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

# The first arg will get eval'd so escape any meta characters
# The 2nd arg is an expected string/response from expr for that op.
test_expr() {
	echo "Expression '${1}', expecting '${2}'"
	res=`eval expr $1 2>&1`
	if [ "$res" != "$2" ]; then
		atf_fail "Expected $2, got $res from expression: " \
		         "`eval echo $1`"
	fi
}

atf_test_case lang
lang_ops_head() {
	atf_set "descr" "Test that expr(1) works with non-C LANG (PR bin/2486)"
}
lang_body() {

	export LANG=nonexistent
	atf_check -s exit:0 -o inline:"21\n" -e empty -x "expr 10 + 11"

	export LANG=ru_RU.KOI8-R
	atf_check -s exit:0 -o inline:"21\n" -e empty -x "expr 10 + 11"
}

atf_test_case overflow
overflow_head() {
	atf_set "descr" "Test overflow cases"
}
overflow_body() {
	test_expr '4611686018427387904 + 4611686018427387903' \
	          '9223372036854775807'
	test_expr '4611686018427387904 + 4611686018427387904' \
	          "expr: integer overflow or underflow occurred for operation '4611686018427387904 + 4611686018427387904'"
	test_expr '4611686018427387904 - -4611686018427387904' \
	          "expr: integer overflow or underflow occurred for operation '4611686018427387904 - -4611686018427387904'"
	test_expr '-4611686018427387904 - 4611686018427387903' \
	          '-9223372036854775807'
	test_expr '-4611686018427387904 - 4611686018427387905' \
	          "expr: integer overflow or underflow occurred for operation '-4611686018427387904 - 4611686018427387905'"
	test_expr '-4611686018427387904 \* 1' '-4611686018427387904'
	test_expr '-4611686018427387904 \* -1' '4611686018427387904'
	test_expr '-4611686018427387904 \* 2' '-9223372036854775808'
	test_expr '-4611686018427387904 \* 3' \
	          "expr: integer overflow or underflow occurred for operation '-4611686018427387904 * 3'"
	test_expr '-4611686018427387904 \* -2' \
	          "expr: integer overflow or underflow occurred for operation '-4611686018427387904 * -2'"
	test_expr '4611686018427387904 \* 1' '4611686018427387904'
	test_expr '4611686018427387904 \* 2' \
	          "expr: integer overflow or underflow occurred for operation '4611686018427387904 * 2'"
	test_expr '4611686018427387904 \* 3' \
	          "expr: integer overflow or underflow occurred for operation '4611686018427387904 * 3'"
}

atf_test_case gtkmm
gtkmm_head() {
	atf_set "descr" "Test from gtk-- configure that cause problems on old expr"
}
gtkmm_body() {
	test_expr '3 \> 3 \| 3 = 3 \& 4 \> 4 \| 3 = 3 \& 4 = 4 \& 5 \>= 5' '1'
	test_expr '3 \> 3 \| 3 = 3 \& 4 \> 4 \| 3 = 3 \& 4 = 4 \& 5 \>= 6' '0'
	test_expr '3 \> 3 \| 3 = 3 \& 4 \> 4 \| 3 = 3 \& 4 = 3 \& 5 \>= 5' '0'
	test_expr '3 \> 3 \| 3 = 3 \& 4 \> 4 \| 3 = 2 \& 4 = 4 \& 5 \>= 5' '0'
	test_expr '3 \> 2 \| 3 = 3 \& 4 \> 4 \| 3 = 3 \& 4 = 4 \& 5 \>= 6' '1'
	test_expr '3 \> 3 \| 3 = 3 \& 4 \> 3 \| 3 = 3 \& 4 = 4 \& 5 \>= 5' '1'
}

atf_test_case colon_vs_math
colon_vs_math_head() {
	atf_set "descr" "Basic precendence test with the : operator vs. math"
}
colon_vs_math_body() {
	test_expr '2 : 4 / 2' '0'
	test_expr '4 : 4 % 3' '1'
}

atf_test_case arithmetic_ops
arithmetic_ops_head() {
	atf_set "descr" "Dangling arithemtic operator"
}
arithmetic_ops_body() {
	# Begin FreeBSD
	atf_expect_fail "the following testcases fail with syntax errors on FreeBSD"
	# End FreeBSD
	test_expr '.java_wrapper : /' '0'
	test_expr '4 : \*' '0'
	test_expr '4 : +' '0'
	test_expr '4 : -' '0'
	test_expr '4 : /' '0'
	test_expr '4 : %' '0'
}

atf_test_case basic_math
basic_math_head() {
	atf_set "descr" "Basic math test"
}
basic_math_body() {
	test_expr '2 + 4 \* 5' '22'
}

atf_test_case basic_functional
basic_functional_head() {
	atf_set "descr" "Basic functional tests"
}
basic_functional_body() {
	test_expr '2' '2'
	test_expr '-4' '-4'
	test_expr 'hello' 'hello'
}

atf_test_case compare_ops_precedence
compare_ops_precedence_head() {
	atf_set "descr" "Compare operator precendence test"
}
compare_ops_precedence_body() {
	test_expr '2 \> 1 \* 17' '0'
}

atf_test_case compare_ops
compare_ops_head() {
	atf_set "descr" "Compare operator tests"
}
compare_ops_body() {
	test_expr '2 \!= 5' '1'
	test_expr '2 \!= 2' '0'
	test_expr '2 \<= 3' '1'
	test_expr '2 \<= 2' '1'
	test_expr '2 \<= 1' '0'
	test_expr '2 \< 3' '1'
	test_expr '2 \< 2' '0'
	test_expr '2 = 2' '1'
	test_expr '2 = 4' '0'
	test_expr '2 \>= 1' '1'
	test_expr '2 \>= 2' '1'
	test_expr '2 \>= 3' '0'
	test_expr '2 \> 1' '1'
	test_expr '2 \> 2' '0'
}

atf_test_case multiply
multiply_head() {
	atf_set "descr" "Test the multiply operator (PR bin/12838)"
}
multiply_body() {
	test_expr '1 \* -1' '-1'
	test_expr '2 \> 1 \* 17' '0'
}

atf_test_case negative
negative_head() {
	atf_set "descr" "Test the additive inverse"
}
negative_body() {
	test_expr '-1 + 5' '4'
	test_expr '- 1 + 5' 'expr: syntax error'

	test_expr '5 + -1' '4'
	test_expr '5 + - 1' 'expr: syntax error'

	test_expr '1 - -5' '6'
}

atf_test_case math_precedence
math_precedence_head() {
	atf_set "descr" "More complex math test for precedence"
}
math_precedence_body() {
	test_expr '-3 + -1 \* 4 + 3 / -6' '-7'
}

atf_test_case precedence
precedence_head() {
	atf_set "descr" "Test precedence"
}
precedence_body() {
	# This is messy but the shell escapes cause that
	test_expr 'X1/2/3 : X\\\(.\*[^/]\\\)//\*[^/][^/]\*/\*$ \| . : \\\(.\\\)' '1/2'
}

atf_test_case regex
regex_head() {
	atf_set "descr" "Test proper () returning \1 from a regex"
}
regex_body() {
	# This is messy but the shell escapes cause that
	test_expr '1/2 : .\*/\\\(.\*\\\)' '2'
}

atf_init_test_cases()
{
	atf_add_test_case lang
	atf_add_test_case overflow
	atf_add_test_case gtkmm
	atf_add_test_case colon_vs_math
	atf_add_test_case arithmetic_ops
	atf_add_test_case basic_math
	atf_add_test_case basic_functional
	atf_add_test_case compare_ops_precedence
	atf_add_test_case compare_ops
	atf_add_test_case multiply
	atf_add_test_case negative
	atf_add_test_case math_precedence
	atf_add_test_case precedence
	atf_add_test_case regex
}
