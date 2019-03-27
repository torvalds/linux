# $NetBSD: t_cut.sh,v 1.1 2012/03/17 16:33:13 jruoho Exp $
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

h_run()
{
	file="${1}"; shift
	opts="${*}"

	for fields in 1 2 3 1-2 2,3 4 1-3,4-7 1,2-7
	do
		opts="-f ${fields} $@"
		echo "----- test: cut ${opts} $(basename $file) -----"
		cut $opts "$file" || atf_fail "command failed: cut ${opts} $file"
	done
}

h_check()
{
	diff -Nru "$1" "$2" || atf_fail "files $1 and $2 differ"
}

atf_test_case basic
basic_head()
{
	atf_set "descr" "Checks basic functionality"
}
basic_body()
{
	h_run "$(atf_get_srcdir)/d_cut.in" > out
	h_check out "$(atf_get_srcdir)/d_basic.out"
}

atf_test_case sflag
sflag_head()
{
	atf_set "descr" "Checks -s flag"
}
sflag_body()
{
	h_run "$(atf_get_srcdir)/d_cut.in" -s > out
	h_check out "$(atf_get_srcdir)/d_sflag.out"
}

atf_test_case dflag
dflag_head()
{
	atf_set "descr" "Checks -d flag"
}
dflag_body()
{
	h_run "$(atf_get_srcdir)/d_cut.in" -d ":" > out
	h_check out "$(atf_get_srcdir)/d_dflag.out"
}

atf_test_case dsflag
dsflag_head()
{
	atf_set "descr" "Checks -s and -d flags combined"
}
dsflag_body()
{
	h_run "$(atf_get_srcdir)/d_cut.in" -d ":" -s > out
	h_check out "$(atf_get_srcdir)/d_dsflag.out"
}

atf_test_case latin1
latin1_head()
{
	atf_set "descr" "Checks support for non-ascii characters"
}
latin1_body()
{
	export LC_ALL=C

	atf_check -o inline:"bar\nBar\nBAr\nBAR\n" \
		cut -b 6,7,8 "$(atf_get_srcdir)/d_latin1.in"

	atf_check -o inline:"bar\nBar\nBAr\nBAR\n" \
		cut -c 6,7,8 "$(atf_get_srcdir)/d_latin1.in"
}

atf_test_case utf8
utf8_head()
{
	atf_set "descr" "Checks support for multibyte characters"
}
utf8_body()
{
	export LC_ALL=en_US.UTF-8

	atf_check -o inline:":ba\n:Ba\n:BA\n:BA\n" \
		cut -b 6,7,8 "$(atf_get_srcdir)/d_utf8.in"

	atf_check -o inline:"bar\nBar\nBAr\nBAR\n" \
		cut -c 6,7,8 "$(atf_get_srcdir)/d_utf8.in"
}

atf_init_test_cases()
{
	atf_add_test_case basic
	atf_add_test_case sflag
	atf_add_test_case dflag
	atf_add_test_case dsflag
	atf_add_test_case latin1
	atf_add_test_case utf8
}
