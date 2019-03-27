# $NetBSD: t_rc_d_cli.sh,v 1.4 2010/11/07 17:51:21 jmmv Exp $
#
# Copyright (c) 2010 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Julio Merino.
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

atf_test_case no_command
no_command_head() {
	atf_set "descr" "Tests that the lack of a command errors out"
}
no_command_body() {
	export h_simple=YES
	rc_helper=$(atf_get_srcdir)/h_simple

	atf_check -s eq:1 -o empty -e ignore ${rc_helper}
}

atf_test_case default_start_no_args
default_start_no_args_head() {
	atf_set "descr" "Tests that running the default 'start' without" \
	    "arguments does not error out"
}
default_start_no_args_body() {
	export h_simple=YES
	rc_helper=$(atf_get_srcdir)/h_simple

	atf_check -s eq:0 -o ignore -e empty ${rc_helper} start
	${rc_helper} forcestop
}

atf_test_case default_start_with_args
default_start_with_args_head() {
	atf_set "descr" "Tests that running the default 'start' with" \
	    "arguments errors out"
}
default_start_with_args_body() {
	export h_simple=YES
	rc_helper=$(atf_get_srcdir)/h_simple

	atf_check -s eq:1 -o ignore -e ignore ${rc_helper} start foo
	if ${rc_helper} status >/dev/null; then
		${rc_helper} forcestop
		atf_fail 'extra argument to start did not error out'
	fi
}

atf_test_case default_stop_no_args
default_stop_no_args_head() {
	atf_set "descr" "Tests that running the default 'stop' without" \
	    "arguments does not error out"
}
default_stop_no_args_body() {
	export h_simple=YES
	rc_helper=$(atf_get_srcdir)/h_simple

	${rc_helper} start
	atf_check -s eq:0 -o ignore -e empty ${rc_helper} stop
}

atf_test_case default_stop_with_args
default_stop_with_args_head() {
	atf_set "descr" "Tests that running the default 'stop' with" \
	    "arguments errors out"
}
default_stop_with_args_body() {
	export h_simple=YES
	rc_helper=$(atf_get_srcdir)/h_simple

	${rc_helper} start
	atf_check -s eq:1 -o ignore -e ignore ${rc_helper} stop foo
	if ${rc_helper} status >/dev/null; then
		${rc_helper} forcestop
	else
		atf_fail 'extra argument to stop did not error out'
	fi
}

atf_test_case default_restart_no_args
default_restart_no_args_head() {
	atf_set "descr" "Tests that running the default 'restart' without" \
	    "arguments does not error out"
}
default_restart_no_args_body() {
	export h_simple=YES
	rc_helper=$(atf_get_srcdir)/h_simple

	${rc_helper} start
	atf_check -s eq:0 -o ignore -e empty ${rc_helper} restart
	${rc_helper} forcestop
}

atf_test_case default_restart_with_args
default_restart_with_args_head() {
	atf_set "descr" "Tests that running the default 'restart' with" \
	    "arguments errors out"
}
default_restart_with_args_body() {
	export h_simple=YES
	rc_helper=$(atf_get_srcdir)/h_simple

	${rc_helper} start
	atf_check -s eq:1 -o ignore -e ignore ${rc_helper} restart foo
	${rc_helper} forcestop
}

do_overriden_no_args() {
	local command="${1}"; shift

	export h_args=YES
	rc_helper=$(atf_get_srcdir)/h_args

	cat >expout <<EOF
pre${command}:.
${command}:.
post${command}:.
EOF
	atf_check -s eq:0 -o file:expout -e empty ${rc_helper} ${command}
}

do_overriden_with_args() {
	local command="${1}"; shift

	export h_args=YES
	rc_helper=$(atf_get_srcdir)/h_args

	cat >expout <<EOF
pre${command}:.
${command}: >arg1< > arg 2 < >arg3< >*<.
post${command}:.
EOF
	atf_check -s eq:0 -o file:expout -e empty ${rc_helper} ${command} \
	    'arg1' ' arg 2 ' 'arg3' '*'
}

atf_test_case overriden_start_no_args
overriden_start_no_args_head() {
	atf_set "descr" "Tests that running a custom 'start' without" \
	    "arguments does not pass any parameters to the command"
}
overriden_start_no_args_body() {
	do_overriden_no_args start
}

atf_test_case overriden_start_with_args
overriden_start_with_args_head() {
	atf_set "descr" "Tests that running a custom 'start' with" \
	    "arguments passes those arguments as parameters to the command"
}
overriden_start_with_args_body() {
	do_overriden_with_args start
}

atf_test_case overriden_stop_no_args
overriden_stop_no_args_head() {
	atf_set "descr" "Tests that running a custom 'stop' without" \
	    "arguments does not pass any parameters to the command"
}
overriden_stop_no_args_body() {
	do_overriden_no_args stop
}

atf_test_case overriden_stop_with_args
overriden_stop_with_args_head() {
	atf_set "descr" "Tests that running a custom 'stop' with" \
	    "arguments passes those arguments as parameters to the command"
}
overriden_stop_with_args_body() {
	do_overriden_with_args stop
}

atf_test_case overriden_restart_no_args
overriden_restart_no_args_head() {
	atf_set "descr" "Tests that running a custom 'restart' without" \
	    "arguments does not pass any parameters to the command"
}
overriden_restart_no_args_body() {
	do_overriden_no_args restart
}

atf_test_case overriden_restart_with_args
overriden_restart_with_args_head() {
	atf_set "descr" "Tests that running a custom 'restart' with" \
	    "arguments passes those arguments as parameters to the command"
}
overriden_restart_with_args_body() {
	do_overriden_with_args restart
}

atf_test_case overriden_custom_no_args
overriden_custom_no_args_head() {
	atf_set "descr" "Tests that running a custom command without" \
	    "arguments does not pass any parameters to the command"
}
overriden_custom_no_args_body() {
	do_overriden_no_args custom
}

atf_test_case overriden_custom_with_args
overriden_custom_with_args_head() {
	atf_set "descr" "Tests that running a custom command with" \
	    "arguments passes those arguments as parameters to the command"
}
overriden_custom_with_args_body() {
	do_overriden_with_args custom
}

atf_init_test_cases()
{
	atf_add_test_case no_command

	atf_add_test_case default_start_no_args
	atf_add_test_case default_start_with_args
	atf_add_test_case default_stop_no_args
	atf_add_test_case default_stop_with_args
	atf_add_test_case default_restart_no_args
	atf_add_test_case default_restart_with_args

	atf_add_test_case overriden_start_no_args
	atf_add_test_case overriden_start_with_args
	atf_add_test_case overriden_stop_no_args
	atf_add_test_case overriden_stop_with_args
	atf_add_test_case overriden_restart_no_args
	atf_add_test_case overriden_restart_with_args
	atf_add_test_case overriden_custom_no_args
	atf_add_test_case overriden_custom_with_args
}
