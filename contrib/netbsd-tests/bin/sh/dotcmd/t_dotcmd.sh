# $NetBSD: t_dotcmd.sh,v 1.2 2016/03/27 14:57:50 christos Exp $
#
# Copyright (c) 2014 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Jarmo Jaakkola.
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

# Test loop and function flow control statements in various scopes in a file
# sourced by a dotcmd in various scopes.  Basically, dotcmd is like #include
# in C/C++ so, for example, if the dotcmd is in a loop's body, a break in
# the sourced file can be used to break out of that loop.

# Note that the standard does not require this, and allows lexically
# scoped interpretation of break/continue (and permits dynamic scope
# as an optional extension.)

cmds='return break continue'
scopes='case compound file for func subshell until while'

case_ids=''

for dot_scope in ${scopes}
do
    for cmd in ${cmds}
    do
        for cmd_scope in ${scopes}
        do
            case_id="${dot_scope}_${cmd}_${cmd_scope}"
	    case_ids="${case_ids} ${case_id}"
            atf_test_case "${case_id}"
            eval "
${case_id}_head()
{
    atf_set 'descr' \\
        'dotcmd in ${dot_scope}, file contains ${cmd} in ${cmd_scope}'
}

${case_id}_body()
{
    srcdir=\$(atf_get_srcdir)
    # for dotcmd to find the sourced files
    PATH=\"\${PATH}:\${srcdir}\"
    atf_check -o file:\"\${srcdir}/out/${case_id}.out\" \\
            \"\${srcdir}/${case_id}\"
}
" # end eval
        done
    done
done

atf_init_test_cases()
{
    for case_id in ${case_ids}
    do
        atf_add_test_case "${case_id}"
    done
}
