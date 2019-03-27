# $NetBSD: t_ciphers.sh,v 1.4 2012/07/14 16:04:06 spz Exp $
#
# Copyright (c) 2008, 2009, 2010 The NetBSD Foundation, Inc.
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

atf_test_case bf
bf_head()
{
	atf_set "descr" "Checks blowfish cipher"
}
bf_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_bftest"
}

atf_test_case cast
cast_head()
{
	atf_set "descr" "Checks CAST cipher"
	atf_set "timeout" "300"
}
cast_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_casttest"
}

atf_test_case des
des_head()
{
	atf_set "descr" "Checks DES cipher (libdes)"
}
des_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_destest"
}

atf_test_case evp
evp_head()
{
	atf_set "descr" "Checks EVP cipher"
}
evp_body()
{
	atf_check -o ignore -e ignore $(atf_get_srcdir)/h_evp_test $(atf_get_srcdir)/evptests.txt
}

atf_test_case rc2
rc2_head()
{
	atf_set "descr" "Checks RC2 cipher"
}
rc2_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_rc2test"
}

atf_test_case rc4
rc4_head()
{
	atf_set "descr" "Checks RC4 cipher"
}
rc4_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_rc4test"
}

atf_test_case idea
idea_head()
{
	atf_set "descr" "Checks IDEA cipher"
}
idea_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_ideatest"
}

atf_test_case rc5
rc5_head()
{
	atf_set "descr" "Checks RC5 cipher"
}
rc5_body()
{
	[ -x "$(atf_get_srcdir)/h_rc5test" ] \
	    || atf_skip "RC5 support not available; system built" \
	                "with MKCRYPTO_RC5=no"
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_rc5test"
}

atf_init_test_cases()
{
	atf_add_test_case bf
	atf_add_test_case cast
	atf_add_test_case des
	atf_add_test_case evp
	atf_add_test_case rc2
	atf_add_test_case rc4
	atf_add_test_case idea
	atf_add_test_case rc5
}
