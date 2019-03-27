# $NetBSD: t_cat.sh,v 1.3 2016/06/16 01:04:58 sevan Exp $
#
# Copyright (c) 2012 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Jukka Ruohonen.
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

atf_test_case align
align_head() {
	atf_set "descr" "Test that cat(1) aligns the output " \
			"right with options '-be' (PR bin/4841)"
}

align_body() {

	atf_check -s ignore -o file:$(atf_get_srcdir)/d_align.out \
		-x "cat -be $(atf_get_srcdir)/d_align.in"
}

atf_test_case nonexistent
nonexistent_head() {
	atf_set "descr" "Test that cat(1) doesn't return zero exit " \
			"status for a nonexistent file (PR bin/3538)"
}

nonexistent_body() {

	atf_check -s not-exit:0 -o empty -e not-empty \
		-x "cat /some/name/that/does/not/exist"
}

# Begin FreeBSD
atf_test_case b_output
b_output_head() {
	atf_set "descr" "Test that cat(1) prints out numbers on non-blank "\
			"lines with '-b'"
}

b_output_body() {
	atf_check -o file:$(atf_get_srcdir)/d_b_output.out \
		cat -b $(atf_get_srcdir)/d_b_output.in
}
# End FreeBSD

atf_test_case se_output
se_output_head() {
	atf_set "descr" "Test that cat(1) prints a $ sign " \
			"on blank lines with options '-se' (PR bin/51250)"
}

se_output_body() {
	atf_check -s ignore -o file:$(atf_get_srcdir)/d_se_output.out \
		-x "cat -se $(atf_get_srcdir)/d_se_output.in"
}

# Begin FreeBSD
atf_test_case s_output
s_output_head() {
	atf_set "descr" "Test that cat(1) squeezes multiple adjacent " \
			"empty lines producing a single spaced output with option '-s'"
}

s_output_body() {
	atf_check -s ignore -o file:$(atf_get_srcdir)/d_s_output.out \
		cat -s $(atf_get_srcdir)/d_s_output.in
}

atf_test_case e_output
e_output_head() {
	atf_set "descr" "Test that cat(1) prints a $ sign " \
			"on blank lines with option '-e'"
}

e_output_body() {
	atf_check -s ignore -o file:$(atf_get_srcdir)/d_se_output.out \
		cat -e $(atf_get_srcdir)/d_se_output.in
}

atf_test_case vt_output
vt_output_head() {
	atf_set "descr" "Test that cat(1) displays non-printing characters, " \
			"namely control characters, tab character and meta-characters " \
			"using options '-vt'"
}

vt_output_body() {
	atf_check -s ignore -o file:$(atf_get_srcdir)/d_vt_output.out \
		cat -vt $(atf_get_srcdir)/d_vt_output.in
}
# End FreeBSD

atf_init_test_cases()
{
	atf_add_test_case align
	atf_add_test_case nonexistent
# Begin FreeBSD
	atf_add_test_case b_output
# End FreeBSD
	atf_add_test_case se_output
# Begin FreeBSD
	atf_add_test_case s_output
	atf_add_test_case e_output
	atf_add_test_case vt_output
# End FreeBSD
}
