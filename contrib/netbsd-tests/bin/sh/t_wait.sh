# $NetBSD: t_wait.sh,v 1.8 2016/03/31 16:22:54 christos Exp $
#
# Copyright (c) 2008, 2009, 2010 The NetBSD Foundation, Inc.
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

atf_test_case basic_wait
basic_wait_head() {
	atf_set "descr" "Tests simple uses of wait"
}
basic_wait_body() {
	atf_require_prog sleep

	atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c \
		'(echo nothing >/dev/null) & wait'

	atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c \
		'(exit 3) & wait $!; S=$?; test $S -eq 3 || {
			echo "status: $S"; exit 1; }'

	atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c \
		'sleep 3 & sleep 2 & sleep 1 & wait'

	atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c \
		'sleep 3 & (exit 2) & sleep 1 & wait'
}

atf_test_case individual
individual_head() {
	atf_set "descr" "Tests that waiting for individual processes works"
}
individual_body() {
	atf_require_prog sleep

	cat >individualhelper.sh <<\EOF
sleep 3 & P1=$!
sleep 1 & P2=$!

wait ${P1}
S=$?
if [ $S -ne 0 ]; then
    echo "Waiting for first process failed: $S"
    exit 1
fi

wait ${P2}
S=$?
if [ $? -ne 0 ]; then
    echo "Waiting for second process failed"
    exit 1
fi

exit 0
EOF
	output=$(${TEST_SH} individualhelper.sh 2>&1)
	[ $? -eq 0 ] || atf_fail "${output}"
}

atf_test_case jobs
jobs_head() {
	atf_set "descr" "Tests that waiting for individual jobs works"
}
jobs_body() {
	# atf-sh confuses wait for some reason; work it around by creating
	# a helper script that executes /bin/sh directly.

	if ! ${TEST_SH} -c 'sleep 1 & wait %1' 2>/dev/null
	then
		atf_skip "No job control support in this shell"
	fi

	cat >individualhelper.sh <<\EOF
sleep 3 &
sleep 1 &

wait %1
if [ $? -ne 0 ]; then
    echo "Waiting for first job failed"
    exit 1
fi

wait %2
if [ $? -ne 0 ]; then
    echo "Waiting for second job failed"
    exit 1
fi

exit 0
EOF
	output=$(${TEST_SH} individualhelper.sh 2>&1)
	[ $? -eq 0 ] || atf_fail "${output}"

	cat >individualhelper.sh <<\EOF
{ sleep 3; exit 3; } &
{ sleep 1; exit 7; } &

wait %1
S=$?
if [ $S -ne 3 ]; then
    echo "Waiting for first job failed - status: $S != 3 (expected)"
    exit 1
fi

wait %2
S=$?
if [ $S -ne 7 ]; then
    echo "Waiting for second job failed - status: $S != 7 (expected)"
    exit 1
fi

exit 0
EOF

	output=$(${TEST_SH} individualhelper.sh 2>&1)
	[ $? -eq 0 ] || atf_fail "${output}"
}

atf_test_case kill
kill_head() {
	atf_set "descr" "Tests that killing the shell while in wait calls trap"
}
kill_body() {
	atf_require_prog sleep
	atf_require_prog kill

	s=killhelper.sh
	z=killhelper.$$ 
	pid=

	# waiting for a specific process that is not a child
	# should return exit status of 127 according to the spec
	# This test is here before the next, to avoid that one
	# entering an infinite loop should the shell have a bug here.

	atf_check -s exit:127 -o empty -e ignore ${TEST_SH} -c 'wait 1'

	cat > "${s}" <<'EOF'

trap "echo SIGHUP" 1
(sleep 5; exit 3) &
sl=$!
wait
S=$?
echo $S
LS=9999
while [ $S -ne 0 ] && [ $S != 127 ]; do
	wait $sl; S=$?; echo $S
	test $S = $LS && { echo "wait repeats..."; exit 2; }
	LS=$S
	done
EOF

	${TEST_SH} $s > $z &
	pid=$!
	sleep 1

	kill -HUP "${pid}"
	wait

	output="$(cat $z | tr '\n' ' ')"

	if [ "$output" != "SIGHUP 129 3 127 " ]; then
		atf_fail "${output} != 'SIGHUP 129 3 127 '"
	fi
}

atf_init_test_cases() {
	atf_add_test_case basic_wait
	atf_add_test_case individual
	atf_add_test_case jobs
	atf_add_test_case kill
}
