# $NetBSD: t_exit.sh,v 1.6 2016/05/07 23:51:30 kre Exp $
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


atf_test_case background
background_head() {
	atf_set "descr" "Tests that sh(1) sets '$?' properly when running " \
			"a command in the background (PR bin/46327)"
}
background_body() {
	atf_check -o match:0 -e empty ${TEST_SH} -c 'true; true & echo $?'
	# atf_expect_fail "PR bin/46327" (now fixed?)
	atf_check -o match:0 -e empty ${TEST_SH} -c 'false; true & echo $?'
}

atf_test_case function
function_head() {
	atf_set "descr" "Tests that \$? is correctly updated inside " \
			"a function"
}
function_body() {
	atf_check -s exit:0 -o match:STATUS=1-0 -e empty \
		${TEST_SH} -c '
			crud() {
				test yes = no

				cat <<-EOF
				STATUS=$?
				EOF
			}
			foo=$(crud)
			echo "${foo}-$?"
		'
}

atf_test_case readout
readout_head() {
	atf_set "descr" "Tests that \$? is correctly updated in a " \
			"compound expression"
}
readout_body() {
	atf_check -s exit:0 -o match:0 -e empty \
		${TEST_SH} -c 'true && ! true | false; echo $?'
}

atf_test_case trap_subshell
trap_subshell_head() {
	atf_set "descr" "Tests that the trap statement in a subshell " \
			"works when the subshell exits"
}
trap_subshell_body() {
	atf_check -s exit:0 -o inline:'exiting\n' -e empty \
	    ${TEST_SH} -c '( trap "echo exiting" EXIT; /usr/bin/true )'
}

atf_test_case trap_zero__implicit_exit
trap_zero__implicit_exit_head() {
	atf_set "descr" "Tests that the trap statement in a subshell in a " \
		"script works when the subshell simply runs out of commands"
}
trap_zero__implicit_exit_body() {
	# PR bin/6764: sh works but ksh does not
	echo '( trap "echo exiting" 0 )' >helper.sh
	atf_check -s exit:0 -o match:exiting -e empty ${TEST_SH} helper.sh
	# test ksh by setting TEST_SH to /bin/ksh and run the entire set...
	# atf_check -s exit:0 -o match:exiting -e empty /bin/ksh helper.sh
}

atf_test_case trap_zero__explicit_exit
trap_zero__explicit_exit_head() {
	atf_set "descr" "Tests that the trap statement in a subshell in a " \
		"script works when the subshell executes an explicit exit"
}
trap_zero__explicit_exit_body() {
	echo '( trap "echo exiting" 0; exit; echo NO_NO_NO )' >helper.sh
	atf_check -s exit:0 -o match:exiting -o not-match:NO_NO -e empty \
		${TEST_SH} helper.sh
	# test ksh by setting TEST_SH to /bin/ksh and run the entire set...
	# atf_check -s exit:0 -o match:exiting -e empty /bin/ksh helper.sh
}

atf_test_case simple_exit
simple_exit_head() {
	atf_set "descr" "Tests that various values for exit status work"
}
# Note: ATF will not allow tests of exit values > 255, even if they would work
simple_exit_body() {
	for N in 0 1 2 3 4 5 6 42 99 101 125 126 127 128 129 200 254 255
	do
		atf_check -s exit:$N -o empty -e empty \
			${TEST_SH} -c "exit $N; echo FOO; echo BAR >&2"
	done
}

atf_test_case subshell_exit
subshell_exit_head() {
	atf_set "descr" "Tests that subshell exit status works and \$? gets it"
}
# Note: ATF will not allow tests of exit values > 255, even if they would work
subshell_exit_body() {
	for N in 0 1 2 3 4 5 6 42 99 101 125 126 127 128 129 200 254 255
	do
		atf_check -s exit:0 -o empty -e empty \
			${TEST_SH} -c "(exit $N); test \$? -eq $N"
	done
}

atf_test_case subshell_background
subshell_background_head() {
	atf_set "descr" "Tests that sh(1) sets '$?' properly when running " \
			"a subshell in the background"
}
subshell_background_body() {
	atf_check -o match:0 -e empty \
		${TEST_SH} -c 'true; (false || true) & echo $?'
	# atf_expect_fail "PR bin/46327" (now fixed?)
	atf_check -o match:0 -e empty \
		${TEST_SH} -c 'false; (false || true) & echo $?'
}

atf_init_test_cases() {
	atf_add_test_case background
	atf_add_test_case function
	atf_add_test_case readout
	atf_add_test_case trap_subshell
	atf_add_test_case trap_zero__implicit_exit
	atf_add_test_case trap_zero__explicit_exit
	atf_add_test_case simple_exit
	atf_add_test_case subshell_exit
	atf_add_test_case subshell_background
}
