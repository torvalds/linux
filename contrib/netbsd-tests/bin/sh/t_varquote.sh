# $NetBSD: t_varquote.sh,v 1.5 2016/03/27 14:50:01 christos Exp $
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
# the implementation of "sh" to test
: ${TEST_SH:="/bin/sh"}

# Variable quoting test.

check() {
	if [ "$1" != "$2" ]
	then
		atf_fail "expected [$2], found [$1]" 1>&2
	fi
}

atf_test_case all
all_head() {
	atf_set "descr" "Basic checks for variable quoting"
}
all_body() {

	cat <<-'EOF' > script.sh
		T=0
		check() {
			T=$((${T} + 1))

			if [ "$1" != "$2" ]
			then
				printf '%s\n' "T${T}: expected [$2], found [$1]"
				exit 1
			fi
		}

								#1
		foo='${a:-foo}'
		check "$foo" '${a:-foo}'
								#2
		foo="${a:-foo}"
		check "$foo" "foo"
								#3
		foo=${a:-"'{}'"}	
		check "$foo" "'{}'"
								#4
		foo=${a:-${b:-"'{}'"}}
		check "$foo" "'{}'"
								#5
		#    ${   }   The ' are inside ".." so are literal (not quotes).
		foo="${a-'}'}"
		check "$foo" "''}"
								#6
		# The rules for quoting in ${var-word} expressions are somewhat
		# weird, in the following there is not one quoted string being
		# assigned to foo (with internally quoted sub-strings), rather
		# it is a mixed quoted/unquoted string, with parts that are
		# quoted, separated by 2 unquoted sections...
		#    qqqqqqqqqq uuuuuuuuuu qq uuuu qqqq
		foo="${a:-${b:-"${c:-${d:-"x}"}}y}"}}z}"
		#   "                                z*"
		#    ${a:-                          }
		#         ${b:-                    }
		#              "                y*"
		#               ${c:-          }
		#                    ${d:-    }
		#                         "x*"
		check "$foo" "x}y}z}"
								#7
		# And believe it or not, this is the one that gives
		# most problems, with 3 different observed outputs...
		#    qqqqq  qq  q		is one interpretation
		#    qqqqq QQQQ q		is another (most common)
		#			(the third is syntax error...)
		foo="${a:-"'{}'"}"
		check "$foo" "'{}'"

	EOF

	OUT=$( ${TEST_SH} script.sh 2>&1 )
	if  [ $? -ne 0 ]
	then
		atf_fail "${OUT}"
	elif [ -n "${OUT}" ]
	then
		atf_fail "script.sh unexpectedly said: ${OUT}"
	fi
}

atf_test_case nested_quotes_multiword
nested_quotes_multiword_head() {
	atf_set "descr" "Tests that having nested quoting in a multi-word" \
	    "string works (PR bin/43597)"
}
nested_quotes_multiword_body() {
	atf_check -s eq:0 -o match:"first-word second-word" -e empty \
	    ${TEST_SH} -c 'echo "${foo:="first-word"} second-word"'
}

atf_test_case default_assignment_with_arith
default_assignment_with_arith_head() {
	atf_set "descr" "Tests default variable assignment with arithmetic" \
	    "string works (PR bin/50827)"
}
default_assignment_with_arith_body() {
	atf_check -s eq:0 -o empty -e empty ${TEST_SH} -c ': "${x=$((1))}"'
	atf_check -s eq:0 -o match:1 -e empty ${TEST_SH} -c 'echo "${x=$((1))}"'
}

atf_init_test_cases() {
	atf_add_test_case all
	atf_add_test_case nested_quotes_multiword
	atf_add_test_case default_assignment_with_arith
}
