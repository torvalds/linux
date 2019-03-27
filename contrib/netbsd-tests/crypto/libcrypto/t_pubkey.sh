# $NetBSD: t_pubkey.sh,v 1.4 2016/10/13 09:25:37 martin Exp $
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

atf_test_case dsa
dsa_head()
{
	atf_set "descr" "Checks DSA cipher"
}
dsa_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_dsatest"
}

atf_test_case dh
dh_head()
{
	atf_set "descr" "Checks Diffie-Hellman key agreement protocol"
}
dh_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_dhtest"
}

atf_test_case rsa
rsa_head()
{
	atf_set "descr" "Checks RSA"
	atf_set "timeout" "420"
}
rsa_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_rsatest"
}

atf_test_case ec
ec_head()
{
	atf_set "descr" "Checks EC cipher"
	atf_set "timeout" "480"
}
ec_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_ectest"
}

atf_test_case ecdh
ecdh_head()
{
	atf_set "descr" "Checks ECDH key agreement protocol"
}
ecdh_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_ecdhtest"
}

atf_test_case ecdsa
ecdsa_head()
{
	atf_set "descr" "Checks ECDSA algorithm"
	atf_set "timeout" "480"
}
ecdsa_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_ecdsatest"
}

atf_test_case srp
srp_head()
{
	atf_set "descr" "Checks SRP key agreement protocol"
}
srp_body()
{
	atf_check -o ignore -e ignore "$(atf_get_srcdir)/h_srptest"
}

atf_init_test_cases()
{
	atf_add_test_case dsa
	atf_add_test_case dh
	atf_add_test_case rsa
	atf_add_test_case ec
	atf_add_test_case ecdh
	atf_add_test_case ecdsa
	atf_add_test_case srp
}
