# $NetBSD: t_hash.sh,v 1.1 2011/01/02 22:03:25 pgoyette Exp $
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

prog()
{
	echo "$(atf_get_srcdir)/h_hash"
}

datadir()
{
	echo "$(atf_get_srcdir)/data"
}

atf_test_case md5
md5_head()
{
	atf_set "descr" "Checks MD5 functions"
}
md5_body()
{
	atf_check -o file:"$(datadir)/md5test-out" -x \
	    "$(prog) -r < $(datadir)/md5test-in"
}

atf_test_case sha1
sha1_head()
{
	atf_set "descr" "Checks SHA1 functions"
}
sha1_body()
{
	atf_check -o file:"$(datadir)/sha1test-out" -x \
	    "$(prog) -rs < $(datadir)/sha1test-in"

	atf_check -o file:"$(datadir)/sha1test2-out" -x \
	    "jot -s '' -b 'a' -n 1000000 | $(prog) -rs"
}

atf_init_test_cases()
{
	atf_add_test_case md5
	atf_add_test_case sha1
}
