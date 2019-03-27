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

# -------------------------------------------------------------------------
# Helper tests for "t_cleanup".
# -------------------------------------------------------------------------

atf_test_case cleanup_pass cleanup
cleanup_pass_head()
{
    atf_set "descr" "Helper test case for the t_cleanup test program"
}
cleanup_pass_body()
{
    touch $(atf_config_get tmpfile)
}
cleanup_pass_cleanup()
{
    if [ $(atf_config_get cleanup no) = yes ]; then
        rm $(atf_config_get tmpfile)
    fi
}

atf_test_case cleanup_fail cleanup
cleanup_fail_head()
{
    atf_set "descr" "Helper test case for the t_cleanup test program"
}
cleanup_fail_body()
{
    touch $(atf_config_get tmpfile)
    atf_fail "On purpose"
}
cleanup_fail_cleanup()
{
    if [ $(atf_config_get cleanup no) = yes ]; then
        rm $(atf_config_get tmpfile)
    fi
}

atf_test_case cleanup_skip cleanup
cleanup_skip_head()
{
    atf_set "descr" "Helper test case for the t_cleanup test program"
}
cleanup_skip_body()
{
    touch $(atf_config_get tmpfile)
    atf_skip "On purpose"
}
cleanup_skip_cleanup()
{
    if [ $(atf_config_get cleanup no) = yes ]; then
        rm $(atf_config_get tmpfile)
    fi
}

atf_test_case cleanup_curdir cleanup
cleanup_curdir_head()
{
    atf_set "descr" "Helper test case for the t_cleanup test program"
}
cleanup_curdir_body()
{
    echo 1234 >oldvalue
}
cleanup_curdir_cleanup()
{
    test -f oldvalue && echo "Old value: $(cat oldvalue)"
}

atf_test_case cleanup_sigterm cleanup
cleanup_sigterm_head()
{
    atf_set "descr" "Helper test case for the t_cleanup test program"
}
cleanup_sigterm_body()
{
    touch $(atf_config_get tmpfile)
    kill $$
    touch $(atf_config_get tmpfile).no
}
cleanup_sigterm_cleanup()
{
    rm $(atf_config_get tmpfile)
}

# -------------------------------------------------------------------------
# Helper tests for "t_config".
# -------------------------------------------------------------------------

atf_test_case config_unset
config_unset_head()
{
    atf_set "descr" "Helper test case for the t_config test program"
}
config_unset_body()
{
    if atf_config_has 'test'; then
        atf_fail "Test variable already defined"
    fi
}

atf_test_case config_empty
config_empty_head()
{
    atf_set "descr" "Helper test case for the t_config test program"
}
config_empty_body()
{
    atf_check_equal "$(atf_config_get 'test')" ""
}

atf_test_case config_value
config_value_head()
{
    atf_set "descr" "Helper test case for the t_config test program"
}
config_value_body()
{
    atf_check_equal "$(atf_config_get 'test')" "foo"
}

atf_test_case config_multi_value
config_multi_value_head()
{
    atf_set "descr" "Helper test case for the t_config test program"
}
config_multi_value_body()
{
    atf_check_equal "$(atf_config_get 'test')" "foo bar"
}

# -------------------------------------------------------------------------
# Helper tests for "t_expect".
# -------------------------------------------------------------------------

atf_test_case expect_pass_and_pass
expect_pass_and_pass_body()
{
    atf_expect_pass
}

atf_test_case expect_pass_but_fail_requirement
expect_pass_but_fail_requirement_body()
{
    atf_expect_pass
    atf_fail "Some reason"
}

atf_test_case expect_pass_but_fail_check
expect_pass_but_fail_check_body()
{
    atf_fail "Non-fatal failures not implemented"
}

atf_test_case expect_fail_and_fail_requirement
expect_fail_and_fail_requirement_body()
{
    atf_expect_fail "Fail reason"
    atf_fail "The failure"
    atf_expect_pass
}

atf_test_case expect_fail_and_fail_check
expect_fail_and_fail_check_body()
{
    atf_fail "Non-fatal failures not implemented"
}

atf_test_case expect_fail_but_pass
expect_fail_but_pass_body()
{
    atf_expect_fail "Fail first"
    atf_expect_pass
}

atf_test_case expect_exit_any_and_exit
expect_exit_any_and_exit_body()
{
    atf_expect_exit -1 "Call will exit"
    exit 0
}

atf_test_case expect_exit_code_and_exit
expect_exit_code_and_exit_body()
{
    atf_expect_exit 123 "Call will exit"
    exit 123
}

atf_test_case expect_exit_but_pass
expect_exit_but_pass_body()
{
    atf_expect_exit -1 "Call won't exit"
}

atf_test_case expect_signal_any_and_signal
expect_signal_any_and_signal_body()
{
    atf_expect_signal -1 "Call will signal"
    kill -9 $$
}

atf_test_case expect_signal_no_and_signal
expect_signal_no_and_signal_body()
{
    atf_expect_signal 1 "Call will signal"
    kill -1 $$
}

atf_test_case expect_signal_but_pass
expect_signal_but_pass_body()
{
    atf_expect_signal -1 "Call won't signal"
}

atf_test_case expect_death_and_exit
expect_death_and_exit_body()
{
    atf_expect_death "Exit case"
    exit 123
}

atf_test_case expect_death_and_signal
expect_death_and_signal_body()
{
    atf_expect_death "Signal case"
    kill -9 $$
}

atf_test_case expect_death_but_pass
expect_death_but_pass_body()
{
    atf_expect_death "Call won't die"
}

atf_test_case expect_timeout_and_hang
expect_timeout_and_hang_head()
{
    atf_set "timeout" "1"
}
expect_timeout_and_hang_body()
{
    atf_expect_timeout "Will overrun"
    sleep 5
}

atf_test_case expect_timeout_but_pass
expect_timeout_but_pass_head()
{
    atf_set "timeout" "1"
}
expect_timeout_but_pass_body()
{
    atf_expect_timeout "Will just exit"
}

# -------------------------------------------------------------------------
# Helper tests for "t_meta_data".
# -------------------------------------------------------------------------

atf_test_case metadata_no_descr
metadata_no_descr_head()
{
    :
}
metadata_no_descr_body()
{
    :
}

atf_test_case metadata_no_head
metadata_no_head_body()
{
    :
}

# -------------------------------------------------------------------------
# Helper tests for "t_srcdir".
# -------------------------------------------------------------------------

atf_test_case srcdir_exists
srcdir_exists_head()
{
    atf_set "descr" "Helper test case for the t_srcdir test program"
}
srcdir_exists_body()
{
    [ -f "$(atf_get_srcdir)/datafile" ] || atf_fail "Cannot find datafile"
}

# -------------------------------------------------------------------------
# Helper tests for "t_result".
# -------------------------------------------------------------------------

atf_test_case result_pass
result_pass_body()
{
    echo "msg"
}

atf_test_case result_fail
result_fail_body()
{
    echo "msg"
    atf_fail "Failure reason"
}

atf_test_case result_skip
result_skip_body()
{
    echo "msg"
    atf_skip "Skipped reason"
}

# -------------------------------------------------------------------------
# Main.
# -------------------------------------------------------------------------

atf_init_test_cases()
{
    # Add helper tests for t_cleanup.
    atf_add_test_case cleanup_pass
    atf_add_test_case cleanup_fail
    atf_add_test_case cleanup_skip
    atf_add_test_case cleanup_curdir
    atf_add_test_case cleanup_sigterm

    # Add helper tests for t_config.
    atf_add_test_case config_unset
    atf_add_test_case config_empty
    atf_add_test_case config_value
    atf_add_test_case config_multi_value

    # Add helper tests for t_expect.
    atf_add_test_case expect_pass_and_pass
    atf_add_test_case expect_pass_but_fail_requirement
    atf_add_test_case expect_pass_but_fail_check
    atf_add_test_case expect_fail_and_fail_requirement
    atf_add_test_case expect_fail_and_fail_check
    atf_add_test_case expect_fail_but_pass
    atf_add_test_case expect_exit_any_and_exit
    atf_add_test_case expect_exit_code_and_exit
    atf_add_test_case expect_exit_but_pass
    atf_add_test_case expect_signal_any_and_signal
    atf_add_test_case expect_signal_no_and_signal
    atf_add_test_case expect_signal_but_pass
    atf_add_test_case expect_death_and_exit
    atf_add_test_case expect_death_and_signal
    atf_add_test_case expect_death_but_pass
    atf_add_test_case expect_timeout_and_hang
    atf_add_test_case expect_timeout_but_pass

    # Add helper tests for t_meta_data.
    atf_add_test_case metadata_no_descr
    atf_add_test_case metadata_no_head

    # Add helper tests for t_srcdir.
    atf_add_test_case srcdir_exists

    # Add helper tests for t_result.
    atf_add_test_case result_pass
    atf_add_test_case result_fail
    atf_add_test_case result_skip
}

# vim: syntax=sh:expandtab:shiftwidth=4:softtabstop=4
