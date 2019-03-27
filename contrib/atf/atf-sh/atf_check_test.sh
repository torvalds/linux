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
# THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
# CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
# INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
# IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
# IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# TODO: Bring in the checks in the bootstrap testsuite for atf_check.

atf_test_case info_ok
info_ok_head()
{
    atf_set "descr" "Verifies that atf_check prints an informative" \
                    "message even when the command is successful"
}
info_ok_body()
{
    h="$(atf_get_srcdir)/misc_helpers -s $(atf_get_srcdir)"

    atf_check -s eq:0 -o save:stdout -e save:stderr -x \
              "${h} atf_check_info_ok"
    grep 'Executing command.*true' stdout >/dev/null || \
        atf_fail "atf_check does not print an informative message"

    atf_check -s eq:0 -o save:stdout -e save:stderr -x \
              "${h} atf_check_info_fail"
    grep 'Executing command.*false' stdout >/dev/null || \
        atf_fail "atf_check does not print an informative message"
}

atf_test_case expout_mismatch
expout_mismatch_head()
{
    atf_set "descr" "Verifies that atf_check prints a diff of the" \
                    "stdout and the expected stdout if the two do not" \
                    "match"
}
expout_mismatch_body()
{
    h="$(atf_get_srcdir)/misc_helpers -s $(atf_get_srcdir)"

    atf_check -s eq:1 -o save:stdout -e save:stderr -x \
              "${h} atf_check_expout_mismatch"
    grep 'Executing command.*echo bar' stdout >/dev/null || \
        atf_fail "atf_check does not print an informative message"
    grep 'stdout does not match golden output' stderr >/dev/null || \
        atf_fail "atf_check does not print the stdout header"
    grep 'stderr' stderr >/dev/null && \
        atf_fail "atf_check prints the stderr header"
    grep '^-foo' stderr >/dev/null || \
        atf_fail "atf_check does not print the stdout's diff"
    grep '^+bar' stderr >/dev/null || \
        atf_fail "atf_check does not print the stdout's diff"
}

atf_test_case experr_mismatch
experr_mismatch_head()
{
    atf_set "descr" "Verifies that atf_check prints a diff of the" \
                    "stderr and the expected stderr if the two do not" \
                    "match"
}
experr_mismatch_body()
{
    h="$(atf_get_srcdir)/misc_helpers -s $(atf_get_srcdir)"

    atf_check -s eq:1 -o save:stdout -e save:stderr -x \
              "${h} atf_check_experr_mismatch"
    grep 'Executing command.*echo bar' stdout >/dev/null || \
        atf_fail "atf_check does not print an informative message"
    grep 'stdout' stderr >/dev/null && \
        atf_fail "atf_check prints the stdout header"
    grep 'stderr does not match golden output' stderr >/dev/null || \
        atf_fail "atf_check does not print the stderr header"
    grep '^-foo' stderr >/dev/null || \
        atf_fail "atf_check does not print the stderr's diff"
    grep '^+bar' stderr >/dev/null || \
        atf_fail "atf_check does not print the stderr's diff"
}

atf_test_case null_stdout
null_stdout_head()
{
    atf_set "descr" "Verifies that atf_check prints a the stdout it got" \
                    "when it was supposed to be null"
}
null_stdout_body()
{
    h="$(atf_get_srcdir)/misc_helpers -s $(atf_get_srcdir)"

    atf_check -s eq:1 -o save:stdout -e save:stderr -x \
              "${h} atf_check_null_stdout"
    grep 'Executing command.*echo.*These.*contents' stdout >/dev/null || \
        atf_fail "atf_check does not print an informative message"
    grep 'stdout not empty' stderr >/dev/null || \
        atf_fail "atf_check does not print the stdout header"
    grep 'stderr' stderr >/dev/null && \
        atf_fail "atf_check prints the stderr header"
    grep 'These are the contents' stderr >/dev/null || \
        atf_fail "atf_check does not print stdout's contents"
}

atf_test_case null_stderr
null_stderr_head()
{
    atf_set "descr" "Verifies that atf_check prints a the stderr it got" \
                    "when it was supposed to be null"
}
null_stderr_body()
{
    h="$(atf_get_srcdir)/misc_helpers -s $(atf_get_srcdir)"

    atf_check -s eq:1 -o save:stdout -e save:stderr -x \
              "${h} atf_check_null_stderr"
    grep 'Executing command.*echo.*These.*contents' stdout >/dev/null || \
        atf_fail "atf_check does not print an informative message"
    grep 'stdout' stderr >/dev/null && \
        atf_fail "atf_check prints the stdout header"
    grep 'stderr not empty' stderr >/dev/null || \
        atf_fail "atf_check does not print the stderr header"
    grep 'These are the contents' stderr >/dev/null || \
        atf_fail "atf_check does not print stderr's contents"
}

atf_test_case equal
equal_head()
{
    atf_set "descr" "Verifies that atf_check_equal works"
}
equal_body()
{
    h="$(atf_get_srcdir)/misc_helpers -s $(atf_get_srcdir)"

    atf_check -s eq:0 -o ignore -e ignore -x "${h} atf_check_equal_ok"

    atf_check -s eq:1 -o ignore -e ignore -x \
        "${h} -r resfile atf_check_equal_fail"
    atf_check -s eq:0 -o ignore -e empty grep '^failed: a != b (a != b)$' \
        resfile

    atf_check -s eq:0 -o ignore -e ignore -x "${h} atf_check_equal_eval_ok"

    atf_check -s eq:1 -o ignore -e ignore -x \
        "${h} -r resfile atf_check_equal_eval_fail"
    atf_check -s eq:0 -o ignore -e empty \
        grep '^failed: \${x} != \${y} (a != b)$' resfile
}

atf_test_case flush_stdout_on_death
flush_stdout_on_death_body()
{
    CONTROL_FILE="$(pwd)/done" "$(atf_get_srcdir)/misc_helpers" \
        -s "$(atf_get_srcdir)" atf_check_flush_stdout >out 2>err &
    pid="${!}"
    while [ ! -f ./done ]; do
        echo "Still waiting for helper to create control file"
        ls
        sleep 1
    done
    kill -9 "${pid}"

    grep 'Executing command.*true' out \
        || atf_fail 'First command not in output'
    grep 'Executing command.*false' out \
        || atf_fail 'Second command not in output'
}

atf_init_test_cases()
{
    atf_add_test_case info_ok
    atf_add_test_case expout_mismatch
    atf_add_test_case experr_mismatch
    atf_add_test_case null_stdout
    atf_add_test_case null_stderr
    atf_add_test_case equal
    atf_add_test_case flush_stdout_on_death
}

# vim: syntax=sh:expandtab:shiftwidth=4:softtabstop=4
