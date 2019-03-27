# Copyright (c) 2010 The NetBSD Foundation, Inc.
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

: ${ATF_SH:="__ATF_SH__"}

create_test_program() {
    local output="${1}"; shift
    echo "#! ${ATF_SH} ${*}" >"${output}"
    cat >>"${output}"
    chmod +x "${output}"
}

atf_test_case no_args
no_args_body()
{
    cat >experr <<EOF
atf-sh: ERROR: No test program provided
atf-sh: See atf-sh(1) for usage details.
EOF
    atf_check -s eq:1 -o ignore -e file:experr "${ATF_SH}"
}

atf_test_case missing_script
missing_script_body()
{
    cat >experr <<EOF
atf-sh: ERROR: The test program 'non-existent' does not exist
EOF
    atf_check -s eq:1 -o ignore -e file:experr "${ATF_SH}" non-existent
}

atf_test_case arguments
arguments_body()
{
    create_test_program tp <<EOF
main() {
    echo ">>>\${0}<<<"
    while test \${#} -gt 0; do
        echo ">>>\${1}<<<"
        shift
    done
    true
}
EOF

    cat >expout <<EOF
>>>./tp<<<
>>> a b <<<
>>>foo<<<
EOF
    atf_check -s eq:0 -o file:expout -e empty ./tp ' a b ' foo

    cat >expout <<EOF
>>>tp<<<
>>> hello bye <<<
>>>foo bar<<<
EOF
    atf_check -s eq:0 -o file:expout -e empty "${ATF_SH}" tp \
        ' hello bye ' 'foo bar'
}

atf_test_case custom_shell__command_line
custom_shell__command_line_body()
{
    cat >expout <<EOF
This is the custom shell
This is the test program
EOF

    cat >custom-shell <<EOF
#! /bin/sh
echo "This is the custom shell"
exec /bin/sh "\${@}"
EOF
    chmod +x custom-shell

    echo 'main() { echo "This is the test program"; }' | create_test_program tp
    atf_check -s eq:0 -o file:expout -e empty "${ATF_SH}" -s ./custom-shell tp
}

atf_test_case custom_shell__shebang
custom_shell__shebang_body()
{
    cat >expout <<EOF
This is the custom shell
This is the test program
EOF

    cat >custom-shell <<EOF
#! /bin/sh
echo "This is the custom shell"
exec /bin/sh "\${@}"
EOF
    chmod +x custom-shell

    echo 'main() { echo "This is the test program"; }' | create_test_program \
        tp "-s$(pwd)/custom-shell"
    atf_check -s eq:0 -o file:expout -e empty ./tp
}

atf_test_case set_e
set_e_head()
{
    atf_set "descr" "Simple test to validate that atf-sh works even when" \
        "set -e is enabled"
}
set_e_body()
{
    cat >custom-shell <<EOF
#! /bin/sh
exec /bin/sh -e "\${@}"
EOF
    chmod +x custom-shell

    cat >tp <<EOF
atf_test_case helper
helper_body() {
    atf_skip "reached"
}
atf_init_test_cases() {
    atf_add_test_case helper
}
EOF
    atf_check -s eq:0 -o match:skipped.*reached \
        "${ATF_SH}" -s ./custom-shell tp helper
}

atf_init_test_cases()
{
    atf_add_test_case no_args
    atf_add_test_case missing_script
    atf_add_test_case arguments
    atf_add_test_case custom_shell__command_line
    atf_add_test_case custom_shell__shebang
    atf_add_test_case set_e
}

# vim: syntax=sh:expandtab:shiftwidth=4:softtabstop=4
