# $NetBSD: t_redircloexec.sh,v 1.3 2016/05/15 15:44:43 kre Exp $
#
# Copyright (c) 2016 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Christos Zoulas.
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

mkhelper() {
	name=$1
	fd=$2
	shift 2

	echo "$@" > ./"${name}1"
	echo "echo ${name}2" ">&${fd}" > ./"${name}2"
}

runhelper() {
	${TEST_SH} "./${1}1"
}

cleanhelper() {
	# not really needed, atf cleans up...
	rm -f ./"${1}1" ./"${1}2" out
}

atf_test_case exec_redir_closed
exec_redir_closed_head() {
	atf_set "descr" "Tests that redirections created by exec are closed on exec"
}
exec_redir_closed_body() {

	mkhelper exec 6 \
		"exec 6> out; echo exec1 >&6; ${TEST_SH} exec2; exec 6>&-"

	atf_check -s exit:0 -o empty -e not-empty ${TEST_SH} ./exec1
	atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} -e ./exec1

	mkhelper exec 9 \
		"exec 9> out; echo exec1 >&9; ${TEST_SH} exec2"

	atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} ./exec1

	mkhelper exec 8 \
		"exec 8> out; printf OK; echo exec1 >&8;" \
		"printf OK; ${TEST_SH} exec2; printf ERR"

	atf_check -s not-exit:0 -o match:OKOK -o not-match:ERR -e not-empty \
		${TEST_SH} -e ./exec1

	mkhelper exec 7 \
		"exec 7> out; printf OK; echo exec1 >&7;" \
		"printf OK; ${TEST_SH} exec2 || printf ERR"

	atf_check -s exit:0 -o match:OKOKERR -e not-empty \
		${TEST_SH} ./exec1

	cleanhelper exec
}

atf_test_case exec_redir_open
exec_redir_open_head() {
	atf_set "descr" "Tests that redirections created by exec can remain open"
}
exec_redir_open_body() {

	mkhelper exec 6 \
		"exec 6> out 6>&6; echo exec1 >&6; ${TEST_SH} exec2; exec 6>&-"

	atf_check -s exit:0 -o empty -e empty ${TEST_SH} ./exec1
	atf_check -s exit:0 -o empty -e empty ${TEST_SH} -e ./exec1

	mkhelper exec 9 \
		"exec 9> out ; echo exec1 >&9; ${TEST_SH} exec2 9>&9"

	atf_check -s exit:0 -o empty -e empty ${TEST_SH} ./exec1

	mkhelper exec 8 \
		"exec 8> out; printf OK; exec 8>&8; echo exec1 >&8;" \
		"printf OK; ${TEST_SH} exec2; printf OK"

	atf_check -s exit:0 -o match:OKOKOK -e empty \
		${TEST_SH} -e ./exec1

	mkhelper exec 7 \
		"exec 7> out; printf OK; echo exec1 >&7;" \
		"printf OK; ${TEST_SH} 7>&7 exec2; printf OK"

	atf_check -s exit:0 -o match:OKOKOK -e empty \
		${TEST_SH} -e ./exec1

	cleanhelper exec
}

atf_test_case loop_redir_open
loop_redir_open_head() {
	atf_set "descr" "Tests that redirections in loops don't close on exec"
}
loop_redir_open_body() {
	mkhelper for 3 "for x in x; do ${TEST_SH} ./for2; done 3>out"
	atf_check -s exit:0 \
		-o empty \
		-e empty \
		${TEST_SH} ./for1
	cleanhelper for
}

atf_test_case compound_redir_open
compound_redir_open_head() {
	atf_set "descr" "Tests that redirections in compound statements don't close on exec"
}
compound_redir_open_body() {
	mkhelper comp 3 "{ ${TEST_SH} ./comp2; } 3>out"
	atf_check -s exit:0 \
		-o empty \
		-e empty \
		${TEST_SH} ./comp1
	cleanhelper comp
}

atf_test_case simple_redir_open
simple_redir_open_head() {
	atf_set "descr" "Tests that redirections in simple commands don't close on exec"
}
simple_redir_open_body() {
	mkhelper simp 4 "${TEST_SH} ./simp2 4>out"
	atf_check -s exit:0 \
		-o empty \
		-e empty \
		${TEST_SH} ./simp1
	cleanhelper simp
}

atf_test_case subshell_redir_open
subshell_redir_open_head() {
	atf_set "descr" "Tests that redirections on subshells don't close on exec"
}
subshell_redir_open_body() {
	mkhelper comp 5 "( ${TEST_SH} ./comp2; ${TEST_SH} ./comp2 ) 5>out"
	atf_check -s exit:0 \
		-o empty \
		-e empty \
		${TEST_SH} ./comp1
	cleanhelper comp
}

atf_init_test_cases() {
	atf_add_test_case exec_redir_closed
	atf_add_test_case exec_redir_open
	atf_add_test_case loop_redir_open
	atf_add_test_case compound_redir_open
	atf_add_test_case simple_redir_open
	atf_add_test_case subshell_redir_open
}
