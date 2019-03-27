# $NetBSD: t_awk.sh,v 1.5 2012/12/10 20:30:06 christos Exp $
#
# Copyright (c) 2012 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Christos Zoulas
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

awk=awk

h_check()
{
	local fname=d_$1
	for sfx in in out awk; do
		cp -r $(atf_get_srcdir)/$fname.$sfx .
	done
	shift 1
	atf_check -o file:$fname.out -x "awk $@ -f $fname.awk < $fname.in"
}

atf_test_case big_regexp

big_regexp_head() {
	atf_set "descr" "Checks matching long regular expressions (PR/33392)"
}

big_regexp_body() {
	h_check big_regexp
}

atf_test_case end

end_head() {
	atf_set "descr" "Checks that the last line of the input" \
	                "is available under END pattern (PR/29659)"
}

end_body() {
	h_check end1
	h_check end2
}

atf_test_case string1

string1_head() {
	atf_set "descr" "Checks escaping newlines in string literals"
}

string1_body() {
	for sfx in out awk; do
		cp -r $(atf_get_srcdir)/d_string1.$sfx .
	done
	atf_check -o file:d_string1.out awk -f d_string1.awk
}

atf_test_case multibyte

multibyte_head() {
	atf_set "descr" "Checks multibyte charsets support" \
	                "in tolower and toupper (PR/36394)"
}

multibyte_body() {
	export LANG=en_US.UTF-8

	h_check tolower
	h_check toupper
}

atf_test_case period

period_head() {
	atf_set "descr" "Checks that the period character is recognised" \
	                "in awk program regardless of locale (bin/42320)"
}

period_body() {
	export LANG=ru_RU.KOI8-R

	h_check period -v x=0.5
}

atf_test_case assign_NF

assign_NF_head() {
	atf_set "descr" 'Checks that assign to NF changes $0 and $n (PR/44063)'
}

assign_NF_body() {
	h_check assign_NF
}

atf_test_case single_char_rs

single_char_rs_head() {
	atf_set "descr" "Test awk(1) with single character RS"
}

single_char_rs_body() {
	atf_check \
		-o "inline:1\n2\n\n3\n\n\n4\n\n" \
		-x "echo 1a2aa3aaa4 | $awk 1 RS=a"
}

atf_test_case two_char_rs

two_char_rs_head() {
	atf_set "descr" "Test awk(1) with two characters RS"
}

two_char_rs_body() {
	atf_check \
		-o "inline:1\n2\n3\n4\n\n" \
		-x "echo 1ab2ab3ab4 | $awk 1 RS=ab"
}

atf_test_case single_char_regex_group_rs

single_char_regex_group_rs_head() {
	atf_set "descr" "Test awk(1) with single character regex group RS"
}

single_char_regex_group_rs_body() {
	atf_check \
		-o "inline:1\n2\n\n3\n\n\n4\n\n" \
		-x "echo 1a2aa3aaa4 | $awk 1 RS='[a]'"
}

atf_test_case two_char_regex_group_rs

two_char_regex_group_rs_head() {
	atf_set "descr" "Test awk(1) with two characters regex group RS"
}

two_char_regex_group_rs_body() {
	atf_check \
		-o "inline:1\n2\n\n3\n\n\n4\n\n" \
		-x "echo 1a2ab3aba4 | $awk 1 RS='[ab]'"
}

atf_test_case single_char_regex_star_rs

single_char_regex_star_rs_head() {
	atf_set "descr" "Test awk(1) with single character regex star RS"
}

single_char_regex_star_rs_body() {
	atf_check \
		-o "inline:1\n2\n3\n4\n\n" \
		-x "echo 1a2aa3aaa4 | $awk 1 RS='a*'"
}

atf_test_case two_char_regex_star_rs

two_char_regex_star_rs_head() {
	atf_set "descr" "Test awk(1) with two characters regex star RS"
}

two_char_regex_star_rs_body() {
	atf_check \
		-o "inline:1\n2\n3\n4\n\n" \
		-x "echo 1a2aa3aaa4 | $awk 1 RS='aa*'"
}

atf_test_case regex_two_star_rs

regex_two_star_rs_head() {
	atf_set "descr" "Test awk(1) with regex two star RS"
}

regex_two_star_rs_body() {
	atf_check \
		-o "inline:1\n2\n3\n4\n\n" \
		-x "echo 1a2ab3aab4 | $awk 1 RS='aa*b*'"
}

atf_test_case regex_or_1_rs

regex_or_1_rs_head() {
	atf_set "descr" "Test awk(1) with regex | case 1 RS"
}

regex_or_1_rs_body() {
	atf_check \
		-o "inline:1a\nc\n\n" \
		-x "echo 1abc | $awk 1 RS='abcde|b'"
}

atf_test_case regex_or_2_rs

regex_or_2_rs_head() {
	atf_set "descr" "Test awk(1) with regex | case 2 RS"
}

regex_or_2_rs_body() {
	atf_check \
		-o "inline:1a\ncdf2\n\n" \
		-x "echo 1abcdf2 | $awk 1 RS='abcde|b'"
}

atf_test_case regex_or_3_rs

regex_or_3_rs_head() {
	atf_set "descr" "Test awk(1) with regex | case 3 RS"
}

regex_or_3_rs_body() {
	atf_check \
		-o "inline:1\n\nf2\n\n" \
		-x "echo 1abcdebf2 | $awk 1 RS='abcde|b'"
}

atf_test_case regex_or_4_rs

regex_or_4_rs_head() {
	atf_set "descr" "Test awk(1) with regex | case 4 RS"
}

regex_or_4_rs_body() {
	atf_check \
		-o "inline:1\nbcdf2\n\n" \
		-x "echo 1abcdf2 | $awk 1 RS='abcde|a'"

}

atf_test_case regex_caret_1_rs

regex_caret_1_rs_head() {
	atf_set "descr" "Test awk(1) with regex ^ case 1 RS"
}

regex_caret_1_rs_body() {
	atf_check \
		-o "inline:\n1a2a3a\n\n" \
		-x "echo a1a2a3a | $awk 1 RS='^a'"

}

atf_test_case regex_caret_2_rs

regex_caret_2_rs_head() {
	atf_set "descr" "Test awk(1) with regex ^ case 2 RS"
}

regex_caret_2_rs_body() {
	atf_check \
		-o "inline:\naa1a2a\n\n" \
		-x "echo aaa1a2a | $awk 1 RS='^a'"

}

atf_test_case regex_dollar_1_rs

regex_dollar_1_rs_head() {
	atf_set "descr" "Test awk(1) with regex $ case 1 RS"
}

regex_dollar_1_rs_body() {
	atf_check \
		-o "inline:a1a2a3a\n\n" \
		-x "echo a1a2a3a | $awk 1 RS='a$'"

}

atf_test_case regex_dollar_2_rs

regex_dollar_2_rs_head() {
	atf_set "descr" "Test awk(1) with regex $ case 2 RS"
}

regex_dollar_2_rs_body() {
	atf_check \
		-o "inline:a1a2aaa\n\n" \
		-x "echo a1a2aaa | $awk 1 RS='a$'"

}

atf_test_case regex_reallocation_rs

regex_reallocation_rs_head() {
	atf_set "descr" "Test awk(1) with regex reallocation RS"
}

regex_reallocation_rs_body() {
	atf_check \
		-o "inline:a\na\na\na\na\na\na\na\na\na10000\n\n" \
		-x "jot -s a 10000 | $awk 'NR>1' RS='999[0-9]'"

}

atf_test_case empty_rs

empty_rs_head() {
	atf_set "descr" "Test awk(1) with empty RS"
}

empty_rs_body() {
	atf_check \
		-o "inline:foo\n" \
		-x "echo foo | $awk 1 RS=''"

}

atf_test_case newline_rs

newline_rs_head() {
	atf_set "descr" "Test awk(1) with newline RS"
}

newline_rs_body() {
	atf_check \
		-o "inline:r1f1:r1f2\nr2f1:r2f2\n" \
		-x "printf '\n\n\nr1f1\nr1f2\n\nr2f1\nr2f2\n\n\n' | $awk '{\$1=\$1}1' RS= OFS=:"
}

atf_test_case modify_subsep

modify_subsep_head() {
	atf_set "descr" "Test awk(1) SUPSEP modification (PR/47306)"
}

modify_subsep_body() {
	atf_check \
		-o "inline:1\n1\n1\n" \
		-x "printf '1\n1 2\n' | \
		$awk '1{ arr[\$1 SUBSEP \$2 SUBSEP ++cnt[\$1]]=1} {for (f in arr) print arr[f];}'"
}

atf_init_test_cases() {

	atf_add_test_case big_regexp
	atf_add_test_case end
	atf_add_test_case string1
	atf_add_test_case multibyte
	atf_add_test_case period
	atf_add_test_case assign_NF

	atf_add_test_case single_char_rs
	atf_add_test_case two_char_rs
	atf_add_test_case single_char_regex_group_rs
	atf_add_test_case two_char_regex_group_rs
	atf_add_test_case two_char_regex_star_rs
	atf_add_test_case single_char_regex_star_rs
	atf_add_test_case regex_two_star_rs
	atf_add_test_case regex_or_1_rs
	atf_add_test_case regex_or_2_rs
	atf_add_test_case regex_or_3_rs
	atf_add_test_case regex_caret_1_rs
	atf_add_test_case regex_caret_2_rs
	atf_add_test_case regex_dollar_1_rs
	atf_add_test_case regex_dollar_2_rs
	atf_add_test_case regex_reallocation_rs
	atf_add_test_case empty_rs
	atf_add_test_case newline_rs
	atf_add_test_case modify_subsep
}
