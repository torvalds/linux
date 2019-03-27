# $NetBSD: t_arith.sh,v 1.5 2016/05/12 14:25:11 kre Exp $
#
# Copyright (c) 2016 The NetBSD Foundation, Inc.
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
# the implementation of "sh" to test
: ${TEST_SH:="/bin/sh"}

# Requirement is to support at least "signed long" whatever that means
# (number of bits in "long" is not specified - but should be at least 32).

# These tests use -o inline:"..." rather than -o match:'...' as we have
# only digits to examine, and it is good to be sure that 1 + 1 really gives 2
# and that 42 or 123 don't look like success because there is a 2 in them.

ARITH_BITS='?'
discover_range()
{
	# cannot use arithmetic "test" operators, range of test in
	# ATF_SHELL (or even TEST_SH) might not be as big as that
	# supported by $(( )) in TEST_SH

	if ! ${TEST_SH} -c ': $(( 0x10000 ))' 2>/dev/null
	then
		# 16 bits or less, or hex unsupported, just give up...
		return
	fi
	test $( ${TEST_SH} -c 'echo $(( 0x1FFFF ))' ) = 131071 || return

	# when attempting to exceed the number of available bits
	# the shell may react in any of 3 (rational) ways
	# 1. syntax error (maybe even core dump...) and fail
	# 2. represent a positive number input as negative value
	# 3. keep the number positive, but not the value expected
	#    (perhaps pegged at the max possible value)
	# any of those may be accompanied by a message to stderr

	# Must check all 3 possibilities for each plausible size
	# Tests do not use 0x8000... because that value can have weird
	# other side effects that are not relevant to discover here.
	# But we do want to try and force the sign bit set.

	if ! ${TEST_SH} -c ': $(( 0xC0000000 ))' 2>/dev/null
	then
		# proobably shell detected overflow and complained
		ARITH_BITS=32
		return
	fi
	if ${TEST_SH} 2>/dev/null \
	    -c 'case $(( 0xC0000000 )); in (-*) exit 0;; esac; exit 1'
	then
		ARITH_BITS=32
		return
	fi
	if ${TEST_SH} -c '[ $(( 0xC0000000 )) != 3221225472 ]' 2>/dev/null
	then
		ARITH_BITS=32
		return
	fi

	if ! ${TEST_SH} -c ': $(( 0xC000000000000000 ))' 2>/dev/null
	then
		ARITH_BITS=64
		return
	fi
	if ${TEST_SH} 2>/dev/null \
	    -c 'case $(( 0xC000000000000000 )); in (-*) exit 0;; esac; exit 1'
	then
		ARITH_BITS=64
		return
	fi
	if ${TEST_SH} 2>/dev/null \
	    -c '[ $((0xC000000000000000)) != 13835058055282163712 ]'
	then
		ARITH_BITS=64
		return
	fi

	if ${TEST_SH} 2>/dev/null -c \
	   '[ $((0x123456781234567812345678)) = 5634002657842756053938493048 ]'
	then
		# just assume... (for now anyway, revisit when it happens...)
		ARITH_BITS=96
		return
	fi
}

atf_test_case constants
constants_head()
{
        atf_set "descr" "Tests that arithmetic expansion can handle constants"
}
constants_body()
{
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $((0x0))'

	# atf_expect_fail "PR bin/50959"
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $((0X0))'
	# atf_expect_pass

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $((000))'

	atf_check -s exit:0 -o inline:'1\n' -e empty \
		${TEST_SH} -c 'echo $(( 000000001 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty \
		${TEST_SH} -c 'echo $(( 0x000000 ))'

	atf_check -s exit:0 -o inline:'99999\n' -e empty \
		${TEST_SH} -c 'echo $((99999))'

	[ ${ARITH_BITS} -gt 44 ] &&
		atf_check -s exit:0 -o inline:'9191919191919\n' -e empty \
			${TEST_SH} -c 'echo $((9191919191919))'

	atf_check -s exit:0 -o inline:'13\n' -e empty ${TEST_SH} -c \
		'echo $(( 0xD ))'
	atf_check -s exit:0 -o inline:'11\n' -e empty ${TEST_SH} -c \
		'echo $(( 013 ))'
	atf_check -s exit:0 -o inline:'7\n' -e empty ${TEST_SH} -c \
		'x=7;echo $(($x))'
	atf_check -s exit:0 -o inline:'9\n' -e empty ${TEST_SH} -c \
		'x=9;echo $((x))'

	atf_check -s exit:0 -o inline:'11\n' -e empty \
		${TEST_SH} -c 'x=0xB; echo $(( $x ))'
	atf_check -s exit:0 -o inline:'27\n' -e empty \
		${TEST_SH} -c 'x=0X1B; echo $(( x ))'
	atf_check -s exit:0 -o inline:'27\n' -e empty \
		${TEST_SH} -c 'X=033; echo $(( $X ))'
	atf_check -s exit:0 -o inline:'219\n' -e empty \
		${TEST_SH} -c 'X=0333; echo $(( X ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty \
		${TEST_SH} -c 'NULL=; echo $(( NULL ))'

	# Not clear if this is 0, nothing, or an error, so omit for now
	# atf_check -s exit:0 -o inline:'0\n' -e empty \
	# 	${TEST_SH} -c 'echo $(( ))'

	# not clear whether this should return 0 or an error, so omit for now
	# atf_check -s exit:0 -o inline:'0\n' -e empty \
	# 	${TEST_SH} -c 'echo $(( UNDEFINED_VAR ))'
}


atf_test_case do_unary_plus
do_unary_plus_head()
{
        atf_set "descr" "Tests that unary plus works as expected"
}
do_unary_plus_body()
{
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( +0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( +1 ))'
	atf_check -s exit:0 -o inline:'6\n' -e empty ${TEST_SH} -c \
		'echo $(( + 6 ))'
	atf_check -s exit:0 -o inline:'4321\n' -e empty ${TEST_SH} -c \
		'echo $(( + 4321 ))'
	atf_check -s exit:0 -o inline:'17185\n' -e empty ${TEST_SH} -c \
		'echo $(( + 0x4321 ))'
}

atf_test_case do_unary_minus
do_unary_minus_head()
{
        atf_set "descr" "Tests that unary minus works as expected"
}
do_unary_minus_body()
{
	atf_check -s exit:0 -o inline:'-1\n' -e empty ${TEST_SH} -c \
		'echo $(( -1 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( - 0 ))'
	atf_check -s exit:0 -o inline:'-1\n' -e empty ${TEST_SH} -c \
		'echo $(( - 1 ))'
	atf_check -s exit:0 -o inline:'-6\n' -e empty ${TEST_SH} -c \
		'echo $(( - 6 ))'
	atf_check -s exit:0 -o inline:'-4321\n' -e empty ${TEST_SH} -c \
		'echo $(( - 4321 ))'
	atf_check -s exit:0 -o inline:'-2257\n' -e empty ${TEST_SH} -c \
		'echo $(( - 04321 ))'
	atf_check -s exit:0 -o inline:'-7\n' -e empty ${TEST_SH} -c \
		'echo $((-7))'
}

atf_test_case do_unary_not
do_unary_not_head()
{
        atf_set "descr" "Tests that unary not (boolean) works as expected"
}
do_unary_not_body()
{
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( ! 1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( ! 0 ))'

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( !1234 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( !0xFFFF ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( ! 000000 ))'
}

atf_test_case do_unary_tilde
do_unary_tilde_head()
{
        atf_set "descr" "Tests that unary not (bitwise) works as expected"
}
do_unary_tilde_body()
{
	# definitely 2's complement arithmetic here...

	atf_check -s exit:0 -o inline:'-1\n' -e empty ${TEST_SH} -c \
		'echo $(( ~ 0 ))'
	atf_check -s exit:0 -o inline:'-2\n' -e empty ${TEST_SH} -c \
		'echo $(( ~ 1 ))'

	atf_check -s exit:0 -o inline:'-1235\n' -e empty ${TEST_SH} -c \
		'echo $(( ~1234 ))'
	atf_check -s exit:0 -o inline:'-256\n' -e empty ${TEST_SH} -c \
		'echo $(( ~0xFF ))'
}

atf_test_case elementary_add
elementary_add_head()
{
        atf_set "descr" "Tests that simple addition works as expected"
}
elementary_add_body()
{
	# some of these tests actually test unary ops &  op precedence...

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 + 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 + 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 + 1 ))'
	atf_check -s exit:0 -o inline:'2\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 + 1 ))'
	atf_check -s exit:0 -o inline:'10\n' -e empty ${TEST_SH} -c \
		'echo $(( 4 + 6 ))'
	atf_check -s exit:0 -o inline:'10\n' -e empty ${TEST_SH} -c \
		'echo $(( 6 + 4 ))'
	atf_check -s exit:0 -o inline:'5555\n' -e empty ${TEST_SH} -c \
		'echo $(( 1234 + 4321 ))'
	atf_check -s exit:0 -o inline:'3333\n' -e empty ${TEST_SH} -c \
		'echo $((1111+2222))'
	atf_check -s exit:0 -o inline:'5555\n' -e empty ${TEST_SH} -c \
		'echo $((+3333+2222))'
	atf_check -s exit:0 -o inline:'7777\n' -e empty ${TEST_SH} -c \
		'echo $((+3333 + +4444))'
	atf_check -s exit:0 -o inline:'-7777\n' -e empty ${TEST_SH} -c \
		'echo -$((+4125+ +3652))'
}

atf_test_case elementary_sub
elementary_sub_head()
{
        atf_set "descr" "Tests that simple subtraction works as expected"
}
elementary_sub_body()
{
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 - 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 - 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 - 1 ))'
	atf_check -s exit:0 -o inline:'-1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 - 1 ))'
	atf_check -s exit:0 -o inline:'488\n' -e empty ${TEST_SH} -c \
		'echo $(( 1066 - 578 ))'
	atf_check -s exit:0 -o inline:'-3662\n' -e empty ${TEST_SH} -c \
		'echo $(( 2016-5678 ))'
	atf_check -s exit:0 -o inline:'-3662\n' -e empty ${TEST_SH} -c \
		'echo $(( 2016+-5678 ))'
	atf_check -s exit:0 -o inline:'-3662\n' -e empty ${TEST_SH} -c \
		'echo $(( 2016-+5678 ))'
	atf_check -s exit:0 -o inline:'-7694\n' -e empty ${TEST_SH} -c \
		'echo $(( -2016-5678 ))'
	atf_check -s exit:0 -o inline:'--1\n' -e empty ${TEST_SH} -c \
		'echo -$(( -1018 - -1017 ))'
}

atf_test_case elementary_mul
elementary_mul_head()
{
        atf_set "descr" "Tests that simple multiplication works as expected"
}
elementary_mul_body()
{
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 * 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 * 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 * 1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 * 1 ))'
	atf_check -s exit:0 -o inline:'-1\n' -e empty ${TEST_SH} -c \
		'echo $(( -1 * 1 ))'
	atf_check -s exit:0 -o inline:'-1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 * -1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( -1 * -1 ))'
	atf_check -s exit:0 -o inline:'391\n' -e empty ${TEST_SH} -c \
		'echo $(( 17 * 23 ))'
	atf_check -s exit:0 -o inline:'169\n' -e empty ${TEST_SH} -c \
		'echo $(( 13*13 ))'
	atf_check -s exit:0 -o inline:'-11264\n' -e empty ${TEST_SH} -c \
		'echo $(( -11 *1024 ))'
	atf_check -s exit:0 -o inline:'-16983\n' -e empty ${TEST_SH} -c \
		'echo $(( 17* -999 ))'
	atf_check -s exit:0 -o inline:'9309\n' -e empty ${TEST_SH} -c \
		'echo $(( -29*-321 ))'
}

atf_test_case elementary_div
elementary_div_head()
{
        atf_set "descr" "Tests that simple division works as expected"
}
elementary_div_body()
{
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 / 1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 / 1 ))'
	test ${ARITH_BITS} -ge 38 &&
	    atf_check -s exit:0 -o inline:'99999999999\n' -e empty \
		${TEST_SH} -c 'echo $(( 99999999999 / 1 ))'
	atf_check -s exit:0 -o inline:'2\n' -e empty ${TEST_SH} -c \
		'echo $(( 2 / 1 ))'

	atf_check -s exit:0 -o inline:'3\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 / 1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 / 2 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 / 3 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 / 4 ))'

	atf_check -s exit:0 -o inline:'173\n' -e empty ${TEST_SH} -c \
		'echo $(( 123456 / 713 ))'
	atf_check -s exit:0 -o inline:'13\n' -e empty ${TEST_SH} -c \
		'echo $(( 169 / 13 ))'
}

atf_test_case elementary_rem
elementary_rem_head()
{
        atf_set "descr" "Tests that simple modulus works as expected"
}
elementary_rem_body()
{
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 % 1 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 % 1 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 2 % 1 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 9999 % 1 ))'

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 % 2 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 % 2 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 2 % 2 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0xFFFF % 2 ))'

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 % 3 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 % 3 ))'
	atf_check -s exit:0 -o inline:'2\n' -e empty ${TEST_SH} -c \
		'echo $(( 2 % 3 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 % 3 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 3123 % 3 ))'

	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 9999 % 2 ))'

	atf_check -s exit:0 -o inline:'107\n' -e empty ${TEST_SH} -c \
		'echo $(( 123456%173 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $((169%13))'
}

atf_test_case elementary_shl
elementary_shl_head()
{
        atf_set "descr" "Tests that simple shift left works as expected"
}
elementary_shl_body()
{
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 << 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 << 1 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 << 17 ))'

	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 << 0 ))'
	atf_check -s exit:0 -o inline:'2\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 << 1 ))'
	atf_check -s exit:0 -o inline:'131072\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 << 17 ))'

	atf_check -s exit:0 -o inline:'2021161080\n' -e empty ${TEST_SH} -c \
		'echo $(( 0x3C3C3C3C << 1 ))'

	test "${ARITH_BITS}" -ge 40 &&
	    atf_check -s exit:0 -o inline:'129354309120\n' -e empty \
		${TEST_SH} -c 'echo $(( 0x3C3C3C3C << 7 ))'
	test "${ARITH_BITS}" -ge 72 &&
	    atf_check -s exit:0 -o inline:'1111145054534149079040\n' \
		-e empty ${TEST_SH} -c 'echo $(( 0x3C3C3C3C << 40 ))'

	return 0
}

atf_test_case elementary_shr
elementary_shr_head()
{
        atf_set "descr" "Tests that simple shift right works as expected"
}
elementary_shr_body()
{
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 >> 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 >> 1 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 >> 17 ))'

	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 >> 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 >> 1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 2 >> 1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 >> 1 ))'

	atf_check -s exit:0 -o inline:'4\n' -e empty ${TEST_SH} -c \
		'echo $(( 0x10 >> 2 ))'
	atf_check -s exit:0 -o inline:'4\n' -e empty ${TEST_SH} -c \
		'echo $(( 022 >> 2 ))'

	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 131072 >> 17 ))'

	test ${ARITH_BITS} -ge 40 &&
		atf_check -s exit:0 -o inline:'8\n' -e empty ${TEST_SH} -c \
			'echo $(( 0x4000000000 >> 35 ))'
	test ${ARITH_BITS} -ge 80 &&
		atf_check -s exit:0 -o inline:'4464\n' -e empty ${TEST_SH} -c \
			'echo $(( 0x93400FACE005C871000 >> 64 ))'

	return 0
}

atf_test_case elementary_eq
elementary_eq_head()
{
        atf_set "descr" "Tests that simple equality test works as expected"
}
elementary_eq_body()
{
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 == 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 == 0000 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 == 0x00 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 == 1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'X=30; Y=0x1E; echo $(( X == Y ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0x1234 == 4660 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0x1234 == 011064 ))'

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 == 1 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 == 0000000000000001 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 == 0x10000000000000 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 == 2 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'X=3; Y=7; echo $(( X == Y ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 1234 == 0x4660 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 01234 == 0x11064 ))'
}
atf_test_case elementary_ne
elementary_ne_head()
{
        atf_set "descr" "Tests that simple inequality test works as expected"
}
elementary_ne_body()
{
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 != 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0x71 != 17 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1234 != 01234 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0x1234 != 01234 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'X=3; echo $(( X != 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'X=3; Y=0x11; echo $(( X != Y ))'

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 != 3 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 != 0x0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0xA != 012 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'X=1; echo $(( X != 1 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'X=0xC; Y=014; echo $(( X != Y ))'
}
atf_test_case elementary_lt
elementary_lt_head()
{
        atf_set "descr" "Tests that simple less than test works as expected"
}
elementary_lt_body()
{
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 < 1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( -1 < 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 < 10 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 100 < 101 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0xA1 < 200 ))'

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 < 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 < 0 ))'

	test ${ARITH_BITS} -ge 40 &&
	    atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0x1BEEFF00D < 0x1FACECAFE ))'

	return 0
}
atf_test_case elementary_le
elementary_le_head()
{
        atf_set "descr" "Tests that simple less or equal test works as expected"
}
elementary_le_body()
{
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 <= 1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( -1 <= 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 <= 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 <= 10 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 100 <= 101 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0xA1 <= 161 ))'

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 <= 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( -100 <= -200 ))'

	test ${ARITH_BITS} -ge 40 &&
	    atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'cost=; AUD=; echo $(( $cost 0x2FEEDBABE <= $AUD 12866927294 ))'

	return 0
}
atf_test_case elementary_gt
elementary_gt_head()
{
        atf_set "descr" "Tests that simple greater than works as expected"
}
elementary_gt_body()
{
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 > 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 > -1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 11 > 012 ))'

	# atf_expect_fail "PR bin/50959"
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 2147483647 > 0X7FFFFF0 ))'
	# atf_expect_pass

	test ${ARITH_BITS} -gt 32 &&
	    atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0x80000000 > 0x7FFFFFFF ))'

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 > 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 > 1 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( -1 > 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 > 10 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 2015 > 2016 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0xA1 > 200 ))'

	test ${ARITH_BITS} -ge 44 &&
	    atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0x7F07F07F0 > 34099628014 ))'

	return 0
}
atf_test_case elementary_ge
elementary_ge_head()
{
        atf_set "descr" "Tests that simple greater or equal works as expected"
}
elementary_ge_body()
{
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 >= 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 >= 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( -100 >= -101 ))'

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( -1 >= 0 ))'
}

atf_test_case fiddle_bits_and
fiddle_bits_and_head()
{
	atf_set "descr" "Test bitwise and operations in arithmetic expressions"
}
fiddle_bits_and_body()
{
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 & 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 & 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 & 1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 & 1 ))'

	atf_check -s exit:0 -o inline:'255\n' -e empty ${TEST_SH} -c \
		'echo $(( 0xFF & 0xFF ))'
	atf_check -s exit:0 -o inline:'255\n' -e empty ${TEST_SH} -c \
		'echo $(( 0xFFFF & 0377 ))'

	test "${ARITH_BITS}" -ge 48 &&
	    atf_check -s exit:0 -o inline:'70377641607203\n' -e empty \
		${TEST_SH} -c 'echo $(( 0x5432FEDC0123 & 0x42871357BAB3 ))'

	return 0
}
atf_test_case fiddle_bits_or
fiddle_bits_or_head()
{
	atf_set "descr" "Test bitwise or operations in arithmetic expressions"
}
fiddle_bits_or_body()
{
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 | 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 | 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 | 1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 | 1 ))'

	atf_check -s exit:0 -o inline:'4369\n' -e empty ${TEST_SH} -c \
		'echo $(( 0x1111 | 0x1111 ))'
	atf_check -s exit:0 -o inline:'255\n' -e empty ${TEST_SH} -c \
		'echo $(( 0xAA | 0125 ))'

	test "${ARITH_BITS}" -ge 48 &&
	    atf_check -s exit:0 -o inline:'95348271856563\n' -e empty \
		${TEST_SH} -c 'echo $(( 0x5432FEDC0123 | 0x42871357BAB3 ))'

	return 0
}
atf_test_case fiddle_bits_xor
fiddle_bits_xor_head()
{
	atf_set "descr" "Test bitwise xor operations in arithmetic expressions"
}
fiddle_bits_xor_body()
{
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 ^ 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 ^ 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 ^ 1 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 ^ 1 ))'

	atf_check -s exit:0 -o inline:'255\n' -e empty ${TEST_SH} -c \
		'echo $(( 0xF0 ^ 0x0F ))'
	atf_check -s exit:0 -o inline:'15\n' -e empty ${TEST_SH} -c \
		'echo $(( 0xF0 ^ 0xFF ))'

	test "${ARITH_BITS}" -ge 48 &&
	    atf_check -s exit:0 -o inline:'24970630249360\n' -e empty \
		${TEST_SH} -c 'echo $(( 0x5432FEDC0123 ^ 0x42871357BAB3 ))'

	return 0
}

atf_test_case logical_and
logical_and_head()
{
	atf_set "descr" "Test logical and operations in arithmetic expressions"
}
logical_and_body()
{
	# cannot test short-circuit eval until sh implements side effects...

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 && 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 && 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 && 1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 && 1 ))'

	# atf_expect_fail "PR bin/50960"
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0x1111 && 01234 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0xFFFF && 0xF0F0 ))'
}
atf_test_case logical_or
logical_or_head()
{
	atf_set "descr" "Test logical or operations in arithmetic expressions"
}
logical_or_body()
{
	# cannot test short-circuit eval until sh implements side effects...

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 || 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 || 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 || 1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 || 1 ))'

	# atf_expect_fail "PR bin/50960"
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0x1111 || 01234 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0x33 || 0xF0F0 ))'
}

atf_test_case make_selection
make_selection_head()
{
	atf_set "descr" "Test ?: operator in arithmetic expressions"
}
make_selection_body()
{
	# atf_expect_fail "PR bin/50958"

	atf_check -s exit:0 -o inline:'3\n' -e empty ${TEST_SH} -c \
		'echo $(( 0 ? 2 : 3 ))'
	atf_check -s exit:0 -o inline:'2\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 ? 2 : 3 ))'

	atf_check -s exit:0 -o inline:'111\n' -e empty ${TEST_SH} -c \
		'echo $(( 0x1234 ? 111 : 222 ))'

	atf_check -s exit:0 -o inline:'-1\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 < 2 ? -1 : 1 > 2 ? 1 : 0 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 < 1 ? -1 : 1 > 1 ? 1 : 0 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 2 < 1 ? -1 : 2 > 1 ? 1 : 0 ))'
}

atf_test_case operator_precedence
operator_precedence_head()
{
	atf_set "descr" "Test operator precedence without parentheses"
}
operator_precedence_body()
{
	# NB: apart from $(( ))  ** NO ** parentheses in the expressions.

	atf_check -s exit:0 -o inline:'6\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 + 2 + 3 ))'
	atf_check -s exit:0 -o inline:'2\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 - 2 + 3 ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 - 2 - 1 ))'
	atf_check -s exit:0 -o inline:'2\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 - 2 + 1 ))'

	atf_check -s exit:0 -o inline:'-1\n' -e empty ${TEST_SH} -c \
		'echo $(( - 2 + 1 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 2 + -1 ))'

	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( ! 2 + 1 ))'
	atf_check -s exit:0 -o inline:'2\n' -e empty ${TEST_SH} -c \
		'echo $(( 2 + !1 ))'

	atf_check -s exit:0 -o inline:'8\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 * 2 + 2 ))'
	atf_check -s exit:0 -o inline:'7\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 + 2 * 2 ))'
	atf_check -s exit:0 -o inline:'12\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 * 2 * 2 ))'

	atf_check -s exit:0 -o inline:'5\n' -e empty ${TEST_SH} -c \
		'echo $(( 9 / 3 + 2 ))'
	atf_check -s exit:0 -o inline:'10\n' -e empty ${TEST_SH} -c \
		'echo $(( 9 + 3 / 2 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 9 / 3 / 2 ))'

	atf_check -s exit:0 -o inline:'72\n' -e empty ${TEST_SH} -c \
		'echo $(( 9 << 1 + 2 ))'
	atf_check -s exit:0 -o inline:'48\n' -e empty ${TEST_SH} -c \
		'echo $(( 9 + 3 << 2 ))'
	atf_check -s exit:0 -o inline:'288\n' -e empty ${TEST_SH} -c \
		'echo $(( 9 << 3 << 2 ))'

	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 9 >> 1 + 2 ))'
	atf_check -s exit:0 -o inline:'3\n' -e empty ${TEST_SH} -c \
		'echo $(( 9 + 3 >> 2 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 19 >> 3 >> 1 ))'

	atf_check -s exit:0 -o inline:'4\n' -e empty ${TEST_SH} -c \
		'echo $(( 19 >> 3 << 1 ))'
	atf_check -s exit:0 -o inline:'76\n' -e empty ${TEST_SH} -c \
		'echo $(( 19 << 3 >> 1 ))'

	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 2 + 3 < 3 * 2 ))'
	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 2 << 3 >= 3 << 2 ))'

	# sh inherits C's crazy operator precedence...

	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 0xfD & 0xF == 0xF ))'
}

parentheses_head()
{
	atf_set "descr" "Test use of () to group sub-expressions"
}
parentheses_body()
{
	atf_check -s exit:0 -o inline:'6\n' -e empty ${TEST_SH} -c \
		'echo $(( (1 + 2) + 3 ))'
	atf_check -s exit:0 -o inline:'-4\n' -e empty ${TEST_SH} -c \
		'echo $(( 1 - (2 + 3) ))'
	atf_check -s exit:0 -o inline:'2\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 - (2 - 1) ))'
	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 - ( 2 + 1 ) ))'

	atf_check -s exit:0 -o inline:'-3\n' -e empty ${TEST_SH} -c \
		'echo $(( - (2 + 1) ))'

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( ! (2 + 1) ))'

	atf_check -s exit:0 -o inline:'12\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 * (2 + 2) ))'
	atf_check -s exit:0 -o inline:'10\n' -e empty ${TEST_SH} -c \
		'echo $(( (3 + 2) * 2 ))'
	atf_check -s exit:0 -o inline:'12\n' -e empty ${TEST_SH} -c \
		'echo $(( 3 * (2 * 2) ))'

	atf_check -s exit:0 -o inline:'1\n' -e empty ${TEST_SH} -c \
		'echo $(( 9 / (3 + 2) ))'
	atf_check -s exit:0 -o inline:'6\n' -e empty ${TEST_SH} -c \
		'echo $(( ( 9 + 3 ) / 2 ))'
	atf_check -s exit:0 -o inline:'9\n' -e empty ${TEST_SH} -c \
		'echo $(( 9 / ( 3 / 2 ) ))'

	atf_check -s exit:0 -o inline:'20\n' -e empty ${TEST_SH} -c \
		'echo $(( ( 9 << 1 ) + 2 ))'
	atf_check -s exit:0 -o inline:'21\n' -e empty ${TEST_SH} -c \
		'echo $(( 9 + (3 << 2) ))'
	atf_check -s exit:0 -o inline:'36864\n' -e empty ${TEST_SH} -c \
		'echo $(( 9 << (3 << 2) ))'

	atf_check -s exit:0 -o inline:'6\n' -e empty ${TEST_SH} -c \
		'echo $(( (9 >> 1) + 2 ))'
	atf_check -s exit:0 -o inline:'9\n' -e empty ${TEST_SH} -c \
		'echo $(( 9 + (3 >> 2) ))'
	atf_check -s exit:0 -o inline:'9\n' -e empty ${TEST_SH} -c \
		'echo $(( 19 >> (3 >> 1) ))'

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( 19 >> (3 << 1) ))'
	atf_check -s exit:0 -o inline:'38\n' -e empty ${TEST_SH} -c \
		'echo $(( 19 << (3 >> 1) ))'

	atf_check -s exit:0 -o inline:'2\n' -e empty ${TEST_SH} -c \
		'echo $(( 2 + (3 < 3) * 2 ))'
	atf_check -s exit:0 -o inline:'32\n' -e empty ${TEST_SH} -c \
		'echo $(( 2 << ((3 >= 3) << 2) ))'

	# sh inherits C's crazy operator precedence...

	atf_check -s exit:0 -o inline:'0\n' -e empty ${TEST_SH} -c \
		'echo $(( (0xfD & 0xF) == 0xF ))'
}

atf_test_case arithmetic_fails
arithmetic_fails_head()
{
	atf_set "descr" "Dummy test to force failure"
}
arithmetic_fails_body()
{
	atf_fail "Cannot estimate number of bits supported by $(( ))"
}

atf_init_test_cases() {

	discover_range

	test "${ARITH_BITS}" = '?' && {
		atf_add_test_case arithmetic_fails
		return 0
	}

	# odd names are to get atf's sort order semi-rational

	atf_add_test_case constants
	atf_add_test_case do_unary_plus
	atf_add_test_case do_unary_minus
	atf_add_test_case do_unary_not
	atf_add_test_case do_unary_tilde
	atf_add_test_case elementary_add
	atf_add_test_case elementary_sub
	atf_add_test_case elementary_mul
	atf_add_test_case elementary_div
	atf_add_test_case elementary_rem
	atf_add_test_case elementary_shl
	atf_add_test_case elementary_shr
	atf_add_test_case elementary_eq
	atf_add_test_case elementary_ne
	atf_add_test_case elementary_lt
	atf_add_test_case elementary_le
	atf_add_test_case elementary_gt
	atf_add_test_case elementary_ge
	atf_add_test_case fiddle_bits_and
	atf_add_test_case fiddle_bits_or
	atf_add_test_case fiddle_bits_xor
	atf_add_test_case logical_and
	atf_add_test_case logical_or
	atf_add_test_case make_selection
	atf_add_test_case operator_precedence
	atf_add_test_case parentheses
	# atf_add_test_case progressive			# build up big expr
	# atf_add_test_case test_errors			# erroneous input
	# atf_add_test_case torture		# hard stuff (if there is any)
	# atf_add_test_case var_assign			# assignment ops
	# atf_add_test_case vulgarity	# truly evil inputs (syntax in vars...)
}
