# $NetBSD: t_shift.sh,v 1.2 2016/05/17 09:05:14 kre Exp $
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

atf_test_case basic_shift_test
basic_shift_test_head() {
	atf_set "descr" "Test correct operation of valid shifts"
}
basic_shift_test_body() {

	for a in			\
	  "one-arg::0:one-arg"		\
	  "one-arg:1:0:one-arg"		\
	  "one-arg:0:1 one-arg"		\
	  "a b c::2 b c:a"		\
	  "a b c:1:2 b c:a"		\
	  "a b c:2:1 c:a:b"		\
	  "a b c:3:0:a:b:c"		\
	  "a b c:0:3 a b c"		\
	  "a b c d e f g h i j k l m n o p:1:15 b c d e f g h i j k l m n o p"\
	  "a b c d e f g h i j k l m n o p:9:7 j k l m n o p:a:b:c:g:h:i"     \
	  "a b c d e f g h i j k l m n o p:13:3 n o p:a:b:c:d:k:l:m"	      \
	  "a b c d e f g h i j k l m n o p:16:0:a:b:c:d:e:f:g:h:i:j:k:l:m:n:o:p"
	do
		oIFS="${IFS}"
		IFS=:; set -- $a
		IFS="${oIFS}"

		init="$1"; n="$2"; res="$3"; shift 3

		not=
		for b
		do
			not="${not} -o not-match:$b"
		done

		atf_check -s exit:0 -o "match:${res}" ${not} -e empty \
			${TEST_SH} -c "set -- ${init}; shift $n;"' echo "$# $*"'
	done

	atf_check -s exit:0 -o match:complete -o not-match:ERR -e empty \
		${TEST_SH} -c \
    'set -- a b c d e;while [ $# -gt 0 ];do shift||echo ERR;done;echo complete'
}

atf_test_case excessive_shift
excessive_shift_head() {
	atf_set "descr" "Test acceptable operation of shift too many"
}
# In:
#
#	http://pubs.opengroup.org/onlinepubs/9699919799
#		/utilities/V3_chap02.html#tag_18_26_01
#
# (that URL should be one line, with the /util... immediately after ...9799)
#
# POSIX says of shift (in the "EXIT STATUS" paragraph):
#
#  If the n operand is invalid or is greater than "$#", this may be considered
#  a syntax error and a non-interactive shell may exit; if the shell does not
#  exit in this case, a non-zero exit status shall be returned.
#  Otherwise, zero shall be returned.
#
# NetBSD's sh treats it as an error and exits (if non-interactive, as here),
# other shells do not.
#
# Either behaviour is acceptable - so the test allows for both
# (and checks that if the shell does not exit, "shift" returns status != 0)

excessive_shift_body() {
	for a in				\
		"one-arg:2"			\
		"one-arg:4"			\
		"one-arg:13"			\
		"one two:3"			\
		"one two:7"			\
		"one two three four five:6"	\
		"I II III IV V VI VII VIII IX X XI XII XIII XIV XV:16"	\
		"I II III IV V VI VII VIII IX X XI XII XIII XIV XV:17"	\
		"I II III IV V VI VII VIII IX X XI XII XIII XIV XV:30"	\
		"I II III IV V VI VII VIII IX X XI XII XIII XIV XV:9999"
	do
		oIFS="${IFS}"
		IFS=:; set -- $a
		IFS="${oIFS}"

		atf_check -s not-exit:0 -o match:OK -o not-match:ERR \
			-e ignore ${TEST_SH} -c \
			"set -- $1 ;"'echo OK:$#-'"$2;shift $2 && echo ERR"
	done
}

atf_test_case function_shift
function_shift_head() {
	atf_set "descr" "Test that shift in a function does not affect outside"
}
function_shift_body() {
	: # later...
}

atf_test_case non_numeric_shift
non_numeric_shift_head() {
	atf_set "descr" "Test that non-numeric args to shift are detected"
}

# from the DESCRIPTION section at the URL mentioned with the excessive_shift
# test:
#
#	The value n shall be an unsigned decimal integer ...
#
# That is not hex (octal will be treated as if it were decimal, a leading 0
# will simply be ignored - we test for this by giving an "octal" value that
# would be OK if parsed as octal, but not if parsed (correctly) as decimal)
#
# Obviously total trash like roman numerals or alphabetic strings are out.
#
# Also no signed values (no + or -) and not a string that looks kind of like
# a number,  but only if you're generous
#
# But as the EXIT STATUS section quoted above says, with an invalid 'n'
# the shell has the option of exiting, or returning status != 0, so
# again this test allows both.

non_numeric_shift_body() {

	# there are 9 args set, 010 is 8 if parsed octal, 10 decimal
	for a in a I 0x12 010 5V -1 ' ' '' +1 ' 1'
	do
		atf_check -s not-exit:0 -o empty -e ignore ${TEST_SH} -c \
			"set -- a b c d e f g h i; shift '$a' && echo ERROR"
	done
}

atf_test_case too_many_args
too_many_args_head() {
	# See PR bin/50896
	atf_set "descr" "Test that sh detects invalid extraneous args to shift"
}
# This is a syntax error, a non-interactive shell (us) must exit $? != 0
too_many_args_body() {
	# This tests the bug in PR bin/50896 is fixed

	for a in "1 1" "1 0" "1 2 3" "1 foo" "1 --" "-- 1"
	do
		atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} -c \
			" set -- a b c d; shift ${a} ; echo FAILED "
	done
}

atf_init_test_cases() {
	atf_add_test_case basic_shift_test
	atf_add_test_case excessive_shift
	atf_add_test_case function_shift
	atf_add_test_case non_numeric_shift
	atf_add_test_case too_many_args
}
