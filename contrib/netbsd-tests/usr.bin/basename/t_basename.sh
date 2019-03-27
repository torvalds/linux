# $NetBSD: t_basename.sh,v 1.1 2012/03/17 16:33:12 jruoho Exp $
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

atf_test_case basic
basic_head()
{
	atf_set "descr" "Checks basic functionality"
}
basic_body()
{
	atf_check -o inline:"bin\n" basename /usr/bin
	atf_check -o inline:"usr\n" basename /usr
	atf_check -o inline:"/\n" basename /
	atf_check -o inline:"/\n" basename ///
	atf_check -o inline:"usr\n" basename /usr//
	atf_check -o inline:"bin\n" basename //usr//bin
	atf_check -o inline:"usr\n" basename usr
	atf_check -o inline:"bin\n" basename usr/bin
}

atf_test_case suffix
suffix_head()
{
	atf_set "descr" "Checks removing of provided suffix"
}
suffix_body()
{
	atf_check -o inline:"bi\n" basename /usr/bin n
	atf_check -o inline:"bin\n" basename /usr/bin bin
	atf_check -o inline:"/\n" basename / /
	atf_check -o inline:"g\n" basename /usr/bin/gcc cc
}

atf_init_test_cases()
{
	atf_add_test_case basic
	atf_add_test_case suffix
}
