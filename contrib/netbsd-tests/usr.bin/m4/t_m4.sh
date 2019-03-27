# $NetBSD: t_m4.sh,v 1.1 2012/03/17 16:33:14 jruoho Exp $
#
# Copyright (c) 2008, 2009 The NetBSD Foundation, Inc.
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

atf_test_case eof
eof_head()
{
	atf_set "descr" "Checks that m4 doesn't confuse 0xFF with EOF"
}
eof_body()
{
	cp "$(atf_get_srcdir)/d_ff_after_dnl.m4.uue" .
	uudecode d_ff_after_dnl.m4.uue
	atf_check -o file:"$(atf_get_srcdir)/d_ff_after_dnl.out" \
	    m4 d_ff_after_dnl.m4

	atf_check -o file:"$(atf_get_srcdir)/d_m4wrap.out" \
	    m4 "$(atf_get_srcdir)/d_m4wrap.m4"

	atf_check -o file:"$(atf_get_srcdir)/d_m4wrap-P.out" \
	    m4 -P "$(atf_get_srcdir)/d_m4wrap-P.m4"
}

atf_init_test_cases()
{
	atf_add_test_case eof
}
