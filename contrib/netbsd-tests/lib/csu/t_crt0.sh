# $NetBSD: t_crt0.sh,v 1.4 2011/12/11 14:57:07 joerg Exp $
#
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

atf_test_case initfini1
initfini1_head()
{
	atf_set "descr" "Checks support for init/fini sections"
}
initfini1_body()
{
	cat >expout <<EOF
constructor executed
main executed
destructor executed
EOF

	atf_check -o file:expout "$(atf_get_srcdir)/h_initfini1"
}

atf_test_case initfini2
initfini2_head()
{
	atf_set "descr" "Checks support for init/fini sections in static binaries"
}
initfini2_body()
{
	cat >expout <<EOF
constructor executed
main executed
destructor executed
EOF

	atf_check -o file:expout "$(atf_get_srcdir)/h_initfini2"
}

atf_test_case initfini3
initfini3_head()
{
	atf_set "descr" "Checks support for init/fini sections in dlopen"
}
initfini3_body()
{
	cat >expout <<EOF
constructor executed
main started
constructor2 executed
main after dlopen
destructor2 executed
main terminated
destructor executed
EOF

	atf_check -o file:expout "$(atf_get_srcdir)/h_initfini3"
}

atf_test_case initfini4
initfini4_head()
{
	atf_set "descr" "Checks support for init/fini sections in LD_PRELOAD"
}
initfini4_body()
{
	cat >expout <<EOF
constructor2 executed
constructor executed
main executed
destructor executed
destructor2 executed
EOF

	atf_check -o file:expout -x "env LD_PRELOAD=$(atf_get_srcdir)/h_initfini3_dso.so $(atf_get_srcdir)/h_initfini1"
}

atf_init_test_cases()
{
	atf_add_test_case initfini1
	atf_add_test_case initfini2
	atf_add_test_case initfini3
	atf_add_test_case initfini4
}
