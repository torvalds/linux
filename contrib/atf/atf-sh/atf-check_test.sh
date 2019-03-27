# Copyright (c) 2008 The NetBSD Foundation, Inc.
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

# The Atf_Check and Atf-Shell variables are set by atf-sh.

h_pass()
{
    cmd="$1"; shift

    echo "Running [atf-check $*] against [${cmd}]"

    cat >script.sh <<EOF
#! ${Atf_Shell}
${cmd}
EOF
    chmod +x script.sh

    if ! ${Atf_Check} "${@}" ./script.sh >tmp; then
        cat tmp
        atf_fail "atf-check failed"
    fi
}

h_fail()
{
    cmd="$1"; shift

    echo "Running [atf-check $*] against [${cmd}]"

    cat >script.sh <<EOF
#! ${Atf_Shell}
${cmd}
EOF
    chmod +x script.sh

    if ${Atf_Check} "${@}" ./script.sh 2>tmp; then
        cat tmp
        atf_fail "atf-check succeeded but should fail"
    fi
}

atf_test_case sflag_eq_ne
sflag_eq_ne_head()
{
    atf_set "descr" "Tests for the -s option using the 'eq' and 'ne' qualifiers"
}
sflag_eq_ne_body()
{
    h_pass "true" -s eq:0
    h_pass "false" -s ne:0
    h_pass "exit 255" -s eq:255
    h_pass "exit 0" -s ne:255

    h_fail "exit 256" -s eq:256
    h_fail "exit -1" -s eq:-1
    h_fail "true" -s ne:256
    h_fail "true" -s ne:-1
}

atf_test_case sflag_exit
sflag_exit_head()
{
    atf_set "descr" "Tests for the -s option using the 'exit' qualifier"
}
sflag_exit_body()
{
    h_pass 'true' -s exit:0
    h_pass 'false' -s not-exit:0
    h_pass 'exit 255' -s exit:255
    h_pass 'exit 0' -s not-exit:255

    h_fail 'exit 256' -s exit:256
    h_fail 'exit -1' -s exit:-1
    h_fail 'true' -s not-exit:256
    h_fail 'true' -s not-exit:-1

    h_pass 'true' -s exit
    h_pass 'false' -s exit
    if ${Atf_Check} -s exit -x 'kill $$'; then
        atf_fail "Signal detected as clean exit"
    fi
}

atf_test_case sflag_ignore
sflag_ignore_head()
{
    atf_set "descr" "Tests for the -s option using the 'ignore' qualifier"
}
sflag_ignore_body()
{
    h_pass 'true' -s ignore
    h_pass 'false' -s ignore
    if ${Atf_Check} -s ignored -x 'kill $$'; then
        atf_fail "Signal not ignored"
    fi
}

atf_test_case sflag_signal
sflag_signal_head()
{
    atf_set "descr" "Tests for the -s option using the 'signal' qualifier"
}
sflag_signal_body()
{
    ${Atf_Check} -s signal:hup -x 'kill -1 $$' || atf_fail "Signal not detected"
    ${Atf_Check} -s signal:sighup -x 'kill -1 $$' || atf_fail "Signal not" \
        "detected"
    ${Atf_Check} -s signal:1 -x 'kill -1 $$' || atf_fail "Signal not detected"
    ${Atf_Check} -s signal -x 'kill -1 $$' || atf_fail "Signal not detected"

    ${Atf_Check} -s not-signal:kill -x 'kill -9 $$' && \
        atf_fail "not-signal:kill matched kill -9"
    ${Atf_Check} -s not-signal:kill -x 'kill -1 $$' || \
        atf_fail "not-signal:kill did not match kill -1"

    h_fail 'true' -s signal
    h_fail 'false' -s signal
}

atf_test_case xflag
xflag_head()
{
    atf_set "descr" "Tests for the -x option"
}
xflag_body()
{
    ${Atf_Check} -s ne:0 -o ignore -e ignore "echo foo 2>&1" || \
        atf_fail "Shell command succeeded without -x"

    ${Atf_Check} -e inline:"foo\n" -x "echo foo 1>&2" || \
        atf_fail "Cannot run command with -x"

    ${Atf_Check} -o inline:"foo\n" -x echo foo || \
        atf_fail "Using -x does not respect all provided arguments"
}

atf_test_case oflag_empty
oflag_empty_head()
{
    atf_set "descr" "Tests for the -o option using the 'empty' argument"
}
oflag_empty_body()
{
    h_pass "true" -o empty
    h_fail "echo foo" -o empty
}

atf_test_case oflag_ignore
oflag_ignore_head()
{
    atf_set "descr" "Tests for the -o option using the 'ignore' argument"
}
oflag_ignore_body()
{
    h_pass "true" -o ignore
    h_pass "echo foo" -o ignore
}

atf_test_case oflag_file
oflag_file_head()
{
    atf_set "descr" "Tests for the -o option using the 'file:' argument"
}
oflag_file_body()
{
    touch empty
    h_pass "true" -o file:empty

    echo foo >text
    h_pass "echo foo" -o file:text
    h_fail "echo bar" -o file:text

    dd if=/dev/urandom of=bin bs=1k count=10
    h_pass "cat bin" -o file:bin
}

atf_test_case oflag_inline
oflag_inline_head()
{
    atf_set "descr" "Tests for the -o option using the 'inline:' argument"
}
oflag_inline_body()
{
    h_pass "true" -o inline:
    h_pass "echo foo bar" -o inline:"foo bar\n"
    h_pass "printf 'foo bar'" -o inline:"foo bar"
    h_pass "printf '\t\n\t\n'" -o inline:"\t\n\t\n"
    h_pass "printf '\a\b\033\f\n\r\t\v'" -o inline:"\a\b\e\f\n\r\t\v"
    h_pass "printf '\011\022\033\012'" -o inline:"\011\022\033\012"

    h_fail "echo foo bar" -o inline:"foo bar"
    h_fail "echo -n foo bar" -o inline:"foo bar\n"
}

atf_test_case oflag_match
oflag_match_head()
{
    atf_set "descr" "Tests for the -o option using the 'match:' argument"
}
oflag_match_body()
{
    h_pass "printf no-newline" -o "match:^no-newline"
    h_pass "echo line1; echo foo bar" -o "match:^foo"
    h_pass "echo foo bar" -o "match:o b"
    h_fail "echo foo bar" -o "match:baz"
    h_fail "echo foo bar" -o "match:^bar"
}

atf_test_case oflag_save
oflag_save_head()
{
    atf_set "descr" "Tests for the -o option using the 'save:' argument"
}
oflag_save_body()
{
    h_pass "echo foo" -o save:out
    echo foo >exp
    cmp -s out exp || atf_fail "Saved output does not match expected results"
}

atf_test_case oflag_multiple
oflag_multiple_head()
{
    atf_set "descr" "Tests for multiple occurrences of the -o option"
}
oflag_multiple_body()
{
    h_pass "echo foo bar" -o match:foo -o match:bar
    h_pass "echo foo; echo bar" -o match:foo -o match:bar
    h_fail "echo foo baz" -o match:bar -o match:foo
    h_fail "echo foo; echo baz" -o match:bar -o match:foo
}

atf_test_case oflag_negated
oflag_negated_head()
{
    atf_set "descr" "Tests for negated occurrences of the -o option"
}
oflag_negated_body()
{
    h_fail "echo foo" -o empty
    h_pass "echo foo" -o not-empty

    h_pass "echo foo bar" -o match:foo
    h_fail "echo foo bar" -o not-match:foo
}

atf_test_case eflag_empty
eflag_empty_head()
{
    atf_set "descr" "Tests for the -e option using the 'empty' argument"
}
eflag_empty_body()
{
    h_pass "true 1>&2" -e empty
    h_fail "echo foo 1>&2" -e empty
}

atf_test_case eflag_ignore
eflag_ignore_head()
{
    atf_set "descr" "Tests for the -e option using the 'ignore' argument"
}
eflag_ignore_body()
{
    h_pass "true 1>&2" -e ignore
    h_pass "echo foo 1>&2" -e ignore
}

atf_test_case eflag_file
eflag_file_head()
{
    atf_set "descr" "Tests for the -e option using the 'file:' argument"
}
eflag_file_body()
{
    touch empty
    h_pass "true 1>&2" -e file:empty

    echo foo >text
    h_pass "echo foo 1>&2" -e file:text
    h_fail "echo bar 1>&2" -e file:text

    dd if=/dev/urandom of=bin bs=1k count=10
    h_pass "cat bin 1>&2" -e file:bin
}

atf_test_case eflag_inline
eflag_inline_head()
{
    atf_set "descr" "Tests for the -e option using the 'inline:' argument"
}
eflag_inline_body()
{
    h_pass "true 1>&2" -e inline:
    h_pass "echo foo bar 1>&2" -e inline:"foo bar\n"
    h_pass "printf 'foo bar' 1>&2" -e inline:"foo bar"
    h_pass "printf '\t\n\t\n' 1>&2" -e inline:"\t\n\t\n"
    h_pass "printf '\a\b\033\f\n\r\t\v' 1>&2" -e inline:"\a\b\e\f\n\r\t\v"
    h_pass "printf '\011\022\033\012' 1>&2" -e inline:"\011\022\033\012"

    h_fail "echo foo bar 1>&2" -e inline:"foo bar"
    h_fail "echo -n foo bar 1>&2" -e inline:"foo bar\n"
}

atf_test_case eflag_save
eflag_save_head()
{
    atf_set "descr" "Tests for the -e option using the 'save:' argument"
}
eflag_save_body()
{
    h_pass "echo foo 1>&2" -e save:out
    echo foo >exp
    cmp -s out exp || atf_fail "Saved output does not match expected results"
}

atf_test_case eflag_match
eflag_match_head()
{
    atf_set "descr" "Tests for the -e option using the 'match:' argument"
}
eflag_match_body()
{
    h_pass "printf no-newline 1>&2" -e "match:^no-newline"
    h_pass "echo line1 1>&2; echo foo bar 1>&2" -e "match:^foo"
    h_pass "echo foo bar 1>&2" -e "match:o b"
    h_fail "echo foo bar 1>&2" -e "match:baz"
    h_fail "echo foo bar 1>&2" -e "match:^bar"
}

atf_test_case eflag_multiple
eflag_multiple_head()
{
    atf_set "descr" "Tests for multiple occurrences of the -e option"
}
eflag_multiple_body()
{
    h_pass "echo foo bar 1>&2" -e match:foo -e match:bar
    h_pass "echo foo 1>&2; echo bar 1>&2" -e match:foo -e match:bar
    h_fail "echo foo baz 1>&2" -e match:bar -e match:foo
    h_fail "echo foo 1>&2; echo baz 1>&2" -e match:bar -e match:foo
}

atf_test_case eflag_negated
eflag_negated_head()
{
    atf_set "descr" "Tests for negated occurrences of the -e option"
}
eflag_negated_body()
{
    h_fail "echo foo 1>&2" -e empty
    h_pass "echo foo 1>&2" -e not-empty

    h_pass "echo foo bar 1>&2" -e match:foo
    h_fail "echo foo bar 1>&2" -e not-match:foo
}

atf_test_case stdin
stdin_head()
{
    atf_set "descr" "Tests that stdin is preserved"
}
stdin_body()
{
    echo "hello" | ${Atf_Check} -o match:"hello" cat || \
        atf_fail "atf-check does not seem to respect stdin"
}

atf_test_case invalid_umask
invalid_umask_head()
{
    atf_set "descr" "Tests for a correct error condition if the umask is" \
            "too restrictive"
}
invalid_umask_body()
{
    umask 0222
    ${Atf_Check} false 2>stderr && \
        atf_fail "atf-check returned 0 but it should have failed"
    cat stderr
    grep 'temporary.*current umask.*0222' stderr >/dev/null || \
        atf_fail "atf-check did not report an error related to the" \
                 "current umask"
}

atf_init_test_cases()
{
    atf_add_test_case sflag_eq_ne
    atf_add_test_case sflag_exit
    atf_add_test_case sflag_ignore
    atf_add_test_case sflag_signal

    atf_add_test_case xflag

    atf_add_test_case oflag_empty
    atf_add_test_case oflag_ignore
    atf_add_test_case oflag_file
    atf_add_test_case oflag_inline
    atf_add_test_case oflag_match
    atf_add_test_case oflag_save
    atf_add_test_case oflag_multiple
    atf_add_test_case oflag_negated

    atf_add_test_case eflag_empty
    atf_add_test_case eflag_ignore
    atf_add_test_case eflag_file
    atf_add_test_case eflag_inline
    atf_add_test_case eflag_match
    atf_add_test_case eflag_save
    atf_add_test_case eflag_multiple
    atf_add_test_case eflag_negated

    atf_add_test_case stdin

    atf_add_test_case invalid_umask
}

# vim: syntax=sh:expandtab:shiftwidth=4:softtabstop=4
