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

atf_test_case runtime_warnings
runtime_warnings_head()
{
    # The fact that this test case is in this test program is an abuse.
    atf_set "descr" "Tests that the test case prints a warning because" \
                    "it is being run outside of a runtime engine"
}
runtime_warnings_body()
{
    unset __RUNNING_INSIDE_ATF_RUN
    srcdir="$(atf_get_srcdir)"
    for h in $(get_helpers); do
        atf_check -s eq:0 -o match:"passed" -e match:"WARNING.*kyua" \
            "${h}" -s "${srcdir}" result_pass
    done
}

atf_test_case result_on_stdout
result_on_stdout_head()
{
    atf_set "descr" "Tests that the test case result is printed on stdout" \
                    "by default"
}
result_on_stdout_body()
{
    srcdir="$(atf_get_srcdir)"
    for h in $(get_helpers); do
        atf_check -s eq:0 -o match:"passed" -o match:"msg" \
            -e ignore "${h}" -s "${srcdir}" result_pass
        atf_check -s eq:1 -o match:"failed: Failure reason" -o match:"msg" \
            -e ignore "${h}" -s "${srcdir}" result_fail
        atf_check -s eq:0 -o match:"skipped: Skipped reason" -o match:"msg" \
            -e ignore "${h}" -s "${srcdir}" result_skip
    done
}

atf_test_case result_to_file
result_to_file_head()
{
    atf_set "descr" "Tests that the test case result is sent to a file if -r" \
                    "is used"
}
result_to_file_body()
{
    srcdir="$(atf_get_srcdir)"
    for h in $(get_helpers); do
        atf_check -s eq:0 -o inline:"msg\n" -e ignore "${h}" -s "${srcdir}" \
            -r resfile result_pass
        atf_check -o inline:"passed\n" cat resfile

        atf_check -s eq:1 -o inline:"msg\n" -e ignore "${h}" -s "${srcdir}" \
            -r resfile result_fail
        atf_check -o inline:"failed: Failure reason\n" cat resfile

        atf_check -s eq:0 -o inline:"msg\n" -e ignore "${h}" -s "${srcdir}" \
            -r resfile result_skip
        atf_check -o inline:"skipped: Skipped reason\n" cat resfile
    done
}

atf_test_case result_to_file_fail
result_to_file_fail_head()
{
    atf_set "descr" "Tests controlled failure if the test program fails to" \
        "create the results file"
    atf_set "require.user" "unprivileged"
}
result_to_file_fail_body()
{
    mkdir dir
    chmod 444 dir

    srcdir="$(atf_get_srcdir)"

    for h in $(get_helpers c_helpers cpp_helpers); do
        atf_check -s signal -o ignore \
            -e match:"FATAL ERROR: Cannot create.*'dir/resfile'" \
            "${h}" -s "${srcdir}" -r dir/resfile result_pass
    done

    for h in $(get_helpers sh_helpers); do
        atf_check -s exit -o ignore \
            -e match:"ERROR: Cannot create.*'dir/resfile'" \
            "${h}" -s "${srcdir}" -r dir/resfile result_pass
    done
}

atf_test_case result_exception
result_exception_head()
{
    atf_set "descr" "Tests that an unhandled exception is correctly captured"
}
result_exception_body()
{
    for h in $(get_helpers cpp_helpers); do
        atf_check -s signal -o not-match:'failed: .*This is unhandled' \
            -e ignore "${h}" -s "${srcdir}" result_exception
    done
}

atf_init_test_cases()
{
    atf_add_test_case runtime_warnings
    atf_add_test_case result_on_stdout
    atf_add_test_case result_to_file
    atf_add_test_case result_to_file_fail
    atf_add_test_case result_exception
}

# vim: syntax=sh:expandtab:shiftwidth=4:softtabstop=4
