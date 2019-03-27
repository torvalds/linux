# $NetBSD: t_df_1_noopen.sh,v 1.3 2011/03/17 15:59:32 skrll Exp $
#
# Copyright (c) 2010 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Nick Hudson.
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

atf_test_case df_1_noopen1
df_1_noopen1_head()
{
	atf_set "descr" "Checks DF_1_NOOPEN prevents dlopening of library"
}
df_1_noopen1_body()
{
	cat >expout <<EOF
h_df_1_noopen1: Cannot dlopen non-loadable /usr/lib/libpthread.so
EOF

	atf_check -s exit:1 -e file:expout "$(atf_get_srcdir)/h_df_1_noopen1"
}

atf_test_case df_1_noopen2
df_1_noopen2_head()
{
	atf_set "descr" "Checks DF_1_NOOPEN is allowed on already loaded library"
}
df_1_noopen2_body()
{
	cat >expout <<EOF
libpthread loaded successfully
EOF

	atf_check -o file:expout "$(atf_get_srcdir)/h_df_1_noopen2"
}

atf_init_test_cases()
{
	atf_add_test_case df_1_noopen1
	atf_add_test_case df_1_noopen2
}
