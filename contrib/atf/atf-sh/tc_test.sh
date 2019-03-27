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

atf_test_case default_status
default_status_head()
{
    atf_set "descr" "Verifies that test cases get the correct default" \
                    "status if they did not provide any"
}
default_status_body()
{
    h="$(atf_get_srcdir)/misc_helpers -s $(atf_get_srcdir)"
    atf_check -s eq:0 -o ignore -e ignore ${h} tc_pass_true
    atf_check -s eq:1 -o ignore -e ignore ${h} tc_pass_false
    atf_check -s eq:1 -o match:'failed:.*body.*non-ok exit code' -e ignore \
        ${h} tc_pass_return_error
    atf_check -s eq:1 -o ignore -e match:'An error' ${h} tc_fail
}

atf_test_case missing_body
missing_body_head()
{
    atf_set "descr" "Verifies that test cases without a body are reported" \
                    "as failed"
}
missing_body_body()
{
    h="$(atf_get_srcdir)/misc_helpers -s $(atf_get_srcdir)"
    atf_check -s eq:1 -o ignore -e ignore ${h} tc_missing_body
}

atf_init_test_cases()
{
    atf_add_test_case default_status
    atf_add_test_case missing_body
}

# vim: syntax=sh:expandtab:shiftwidth=4:softtabstop=4
