# $NetBSD: t_uniq.sh,v 1.1 2016/10/22 14:13:39 abhinav Exp $
#
# Copyright (c) 2016 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Abhinav Upadhyay
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
	atf_set "descr" "Checks the basic functionality"
}
basic_body()
{
	atf_check -o file:$(atf_get_srcdir)/d_basic.out uniq \
		$(atf_get_srcdir)/d_basic.in
}

atf_test_case test_counts
test_counts_head()
{
	atf_set "descr" "Tests the -c option, comparing each line of the input" \
		"file data starting from the second field"
}
test_counts_body()
{
	atf_check -o file:$(atf_get_srcdir)/d_counts.out uniq -c -f 1 \
		$(atf_get_srcdir)/d_input.in
}

atf_test_case show_duplicates
show_duplicates_head()
{
	atf_set "descr" "Checks the -d option, comparing each line of the input" \
		"file data starting from the second field"
}
show_duplicates_body()
{
	atf_check -o file:$(atf_get_srcdir)/d_show_duplicates.out uniq -d -f 1 \
		$(atf_get_srcdir)/d_input.in
}

atf_test_case show_uniques
show_uniques_head()
{
	atf_set "descr" "Checks the -u option, comparing each line of the input" \
		"file data starting from the second field"
}
show_uniques_body()
{
	atf_check -o file:$(atf_get_srcdir)/d_show_uniques.out uniq -u -f 1 \
		$(atf_get_srcdir)/d_input.in
}

atf_test_case show_duplicates_from_third_character
show_duplicates_from_third_character_head()
{
	atf_set "descr" "Checks the -d option, comparing each line of the input" \
		"file data starting from the third character (-s option)"
}
show_duplicates_from_third_character_body()
{
	atf_check -o empty uniq -d -s 2 $(atf_get_srcdir)/d_input.in
		
}

atf_init_test_cases()
{
	atf_add_test_case basic
	atf_add_test_case test_counts
	atf_add_test_case show_duplicates
	atf_add_test_case show_uniques
	atf_add_test_case show_duplicates_from_third_character
}
