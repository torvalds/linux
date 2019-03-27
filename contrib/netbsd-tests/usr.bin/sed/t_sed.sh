# $NetBSD: t_sed.sh,v 1.6 2016/04/05 00:48:53 christos Exp $
#
# Copyright (c) 2012 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by Jukka Ruohonen and David A. Holland.
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

atf_test_case c2048
c2048_head() {
	atf_set "descr" "Test that sed(1) does not fail when the " \
			"2048'th character is a backslash (PR bin/25899)"
}

c2048_body() {

	atf_check -s exit:0 -o inline:'foo\n' -e empty \
		-x "echo foo | sed -f $(atf_get_srcdir)/d_c2048.in"
}

atf_test_case emptybackref
emptybackref_head() {
	atf_set "descr" "Test that sed(1) handles " \
			"empty back references (PR bin/28126)"
}

emptybackref_body() {

	atf_check -o inline:"foo1bar1\n" \
		-x "echo foo1bar1 | sed -ne '/foo\(.*\)bar\1/p'"

	atf_expect_fail "PR bin/28126"

	atf_check -o inline:"foobar\n" \
		-x "echo foobar | sed -ne '/foo\(.*\)bar\1/p'"
}

atf_test_case longlines
longlines_head() {
	atf_set "descr" "Test that sed(1) handles " \
			"long lines correctly (PR bin/42261)"
}

longlines_body() {

	str=$(awk 'BEGIN {while(x<2043){printf "x";x++}}')
	echo $str > input

	atf_check -o save:output -x "echo x | sed s,x,${str},g"
	atf_check -s exit:0 -o empty -e empty -x "diff input output"
}

atf_test_case rangeselection
rangeselection_head() {
	atf_set "descr" "Test that sed(1) handles " \
			"range selection correctly"
}

rangeselection_body() {
	# basic cases
	atf_check -o inline:"D\n" \
		-x "printf 'A\nB\nC\nD\n' | sed '1,3d'"
	atf_check -o inline:"A\n" \
		-x "printf 'A\nB\nC\nD\n' | sed '2,4d'"
	# two nonoverlapping ranges
	atf_check -o inline:"C\n" \
		-x "printf 'A\nB\nC\nD\nE\n' | sed '1,2d;4,5d'"
	# overlapping ranges; the first prevents the second from being entered
	atf_check -o inline:"D\nE\n" \
		-x "printf 'A\nB\nC\nD\nE\n' | sed '1,3d;3,5d'"
	# the 'n' command can also prevent ranges from being entered
	atf_check -o inline:"B\nB\nC\nD\n" \
		-x "printf 'A\nB\nC\nD\n' | sed '1,3s/A/B/;1,3n;1,3s/B/C/'"
	atf_check -o inline:"B\nC\nC\nD\n" \
		-x "printf 'A\nB\nC\nD\n' | sed '1,3s/A/B/;1,3n;2,3s/B/C/'"

	# basic cases using regexps
	atf_check -o inline:"D\n" \
		-x "printf 'A\nB\nC\nD\n' | sed '/A/,/C/d'"
	atf_check -o inline:"A\n" \
		-x "printf 'A\nB\nC\nD\n' | sed '/B/,/D/d'"
	# two nonoverlapping ranges
	atf_check -o inline:"C\n" \
		-x "printf 'A\nB\nC\nD\nE\n' | sed '/A/,/B/d;/D/,/E/d'"
	# two overlapping ranges; the first blocks the second as above
	atf_check -o inline:"D\nE\n" \
		-x "printf 'A\nB\nC\nD\nE\n' | sed '/A/,/C/d;/C/,/E/d'"
	# the 'n' command makes some lines invisible to downstreap regexps
	atf_check -o inline:"B\nC\nC\nD\n" \
		-x "printf 'A\nB\nC\nD\n' | sed '/A/,/C/s/A/B/;1,3n;/B/,/C/s/B/C/'"

	# a range ends at the *first* matching end line
	atf_check -o inline:"D\nC\n" \
		-x "printf 'A\nB\nC\nD\nC\n' | sed '/A/,/C/d'"
	# another matching start line within the range has no effect
	atf_check -o inline:"D\nC\n" \
		-x "printf 'A\nB\nA\nC\nD\nC\n' | sed '/A/,/C/d'"
}

atf_test_case preserve_leading_ws_ia
preserve_leading_ws_ia_head() {
	atf_set "descr" "Test that sed(1) preserves leading whitespace " \
			"in insert and append (PR bin/49872)"
}

preserve_leading_ws_ia_body() {
	atf_check -o inline:"    1 2 3\n4 5 6\n    7 8 9\n\n" \
		-x 'echo | sed -e "/^$/i\\
    1 2 3\\
4 5 6\\
    7 8 9"'
}

atf_init_test_cases() {
	atf_add_test_case c2048
	atf_add_test_case emptybackref
	atf_add_test_case longlines
	atf_add_test_case rangeselection
	atf_add_test_case preserve_leading_ws_ia
}
