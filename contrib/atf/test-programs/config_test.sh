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

atf_test_case vflag
vflag_head()
{
    atf_set "descr" "Tests that the -v flag works correctly to set" \
                    "configuration variables"
}
vflag_body()
{
    for h in $(get_helpers); do
        atf_check -s eq:0 -o ignore -e ignore ${h} -s $(atf_get_srcdir) \
            -r resfile config_unset
        atf_check -s eq:0 -o ignore -e empty grep 'passed' resfile

        atf_check -s eq:0 -o ignore -e ignore ${h} -s $(atf_get_srcdir) \
            -r resfile -v 'test=' config_empty
        atf_check -s eq:0 -o ignore -e empty grep 'passed' resfile

        atf_check -s eq:0 -o ignore -e ignore ${h} -s $(atf_get_srcdir) \
            -r resfile -v 'test=foo' config_value
        atf_check -s eq:0 -o ignore -e empty grep 'passed' resfile

        atf_check -s eq:0 -o ignore -e ignore -x ${h} -s $(atf_get_srcdir) \
            -r resfile -v \'test=foo bar\' config_multi_value
        atf_check -s eq:0 -o ignore -e empty grep 'passed' resfile
    done
}

atf_init_test_cases()
{
    atf_add_test_case vflag
}

# vim: syntax=sh:expandtab:shiftwidth=4:softtabstop=4
