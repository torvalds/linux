# $NetBSD: t_basic.sh,v 1.3 2013/08/11 01:50:02 dholland Exp $
#
# Copyright (c) 2013 The NetBSD Foundation, Inc.
# All rights reserved.
#
# This code is derived from software contributed to The NetBSD Foundation
# by David A. Holland.
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

#
# tr -d: delete character
#
atf_test_case dopt
dopt_head() {
	atf_set "descr" "Tests for tr -d"
}

dopt_body() {
	atf_check -o inline:'abcde\n' -x 'echo abcde | tr -d x'
	atf_check -o inline:'abde\n' -x 'echo abcde | tr -d c'
	atf_check -o inline:'ace\n' -x 'echo abcde | tr -d bd'
	atf_check -o inline:'ae\n' -x 'echo abcde | tr -d b-d'
	atf_check -o inline:'b\n' -x 'echo abcde | tr -d ac-e'
	atf_check -o inline:'d\n' -x 'echo abcde | tr -d a-ce'
	atf_check -o inline:'aei\n' -x 'echo abcdefghi | tr -d b-df-h'

	atf_check -o inline:'' -x 'echo abcde | tr -c -d x'
	atf_check -o inline:'c' -x 'echo abcde | tr -c -d c'
	atf_check -o inline:'bd' -x 'echo abcde | tr -c -d bd'
	atf_check -o inline:'bcd' -x 'echo abcde | tr -c -d b-d'
	atf_check -o inline:'acde' -x 'echo abcde | tr -c -d ac-e'
	atf_check -o inline:'abce' -x 'echo abcde | tr -c -d a-ce'
	atf_check -o inline:'bcdfgh' -x 'echo abcdefghi | tr -c -d b-df-h'

	# see if escape codes work
	atf_check -o inline:'splice' -x '(echo spl; echo ice) | tr -d '"'\n'"
	atf_check -o inline:'splice' -x '(echo spl; echo ice) | tr -d '"'\012'"

	# see if escape codes work when followed by other things
	atf_check -o inline:'slice' -x '(echo spl; echo ice) | tr -d '"'\n'p"
	atf_check -o inline:'slice' -x '(echo spl; echo ice) | tr -d '"'\012'p"

	# see if the [=x=] syntax works
	atf_check -o inline:'abde\n' -x 'echo abcde | tr -d '"'[=c=]'"
	atf_check -o inline:'bde\n' -x 'echo abcde | tr -d '"'[=c=]'a"

	# make sure 0 works
	# (ignore stderr as dd blabbers to it)
	atf_check -e ignore -o inline:'ab\n' \
	  -x '(echo -n a; dd if=/dev/zero bs=3 count=1; echo b) | tr -d '"'\0'"

	# test posix classes
	atf_check -o inline:'.\n' -x 'echo aAzZ.123 | tr -d '"'[:alnum:]'"
	atf_check -o inline:'.123\n' -x 'echo aAzZ.123 | tr -d '"'[:alpha:]'"
	atf_check -o inline:'az\n' -x 'echo "a z" | tr -d '"'[:blank:]'"
	atf_check -o inline:'az' -x '(echo a; echo z) | tr -d '"'[:cntrl:]'"
	atf_check -o inline:'aAzZ.\n' -x 'echo aAzZ.123 | tr -d '"'[:digit:]'"
	atf_check -o inline:' \n' -x 'echo "a z.123" | tr -d '"'[:graph:]'"
	atf_check -o inline:'AZ.123\n' -x 'echo aAzZ.123 | tr -d '"'[:lower:]'"
	atf_check -o inline:'\n' -x 'echo aAzZ.123 | tr -d '"'[:print:]'"
	atf_check -o inline:'aAzZ12\n' -x 'echo aAzZ.12 | tr -d '"'[:punct:]'"
	atf_check -o inline:'az' -x 'echo "a z" | tr -d '"'[:space:]'"
	atf_check -o inline:'az.123\n' -x 'echo aAzZ.123 | tr -d '"'[:upper:]'"
	atf_check -o inline:'zZ.\n' -x 'echo aAzZ.123 | tr -d '"'[:xdigit:]'"
}

#
# tr -s: squeeze duplicate character runs
#
atf_test_case sopt
sopt_head() {
	atf_set "descr" "Tests for tr -s"
}

sopt_body() {
	atf_check -o inline:'abcde\n' -x 'echo abcde | tr -s x'
	atf_check -o inline:'abcde\n' -x 'echo abcde | tr -s c'
	atf_check -o inline:'abcde\n' -x 'echo abccccde | tr -s c'
	atf_check -o inline:'abcde\n' -x 'echo abbbcddde | tr -s bd'
	atf_check -o inline:'abcde\n' -x 'echo abbbcccddde | tr -s b-d'

	atf_check -o inline:'acac\n' -x 'echo acac | tr -s c'
	atf_check -o inline:'acac\n' -x 'echo accacc | tr -s c'

	atf_check -o inline:'abcde\n' -x 'echo abcde | tr -c -s x'
	atf_check -o inline:'abcde\n' -x 'echo abcde | tr -c -s c'
	atf_check -o inline:'abcccde\n' -x 'echo abcccde | tr -c -s c'
	atf_check -o inline:'abbbcddde\n' -x 'echo abbbcddde | tr -c -s bd'
	atf_check -o inline:'abbbccddde\n' -x 'echo abbbccddde | tr -c -s b-d'
	atf_check -o inline:'abcccde\n' -x 'echo aaabcccde | tr -c -s b-d'
}

#
# tr -ds: both -d and -s at once
#
atf_test_case dsopt
dsopt_head() {
	atf_set "descr" "Tests for tr -ds"
}

dsopt_body() {
	atf_check -o inline:'abcde\n' -x 'echo abcde | tr -ds x y'
	atf_check -o inline:'abde\n' -x 'echo abcde | tr -ds c x'
	atf_check -o inline:'abcde\n' -x 'echo abcde | tr -ds x c'
	atf_check -o inline:'abde\n' -x 'echo abcde | tr -ds c c'
	atf_check -o inline:'abde\n' -x 'echo abcccde | tr -ds c x'
	atf_check -o inline:'abcde\n' -x 'echo abcccde | tr -ds x c'
	atf_check -o inline:'abde\n' -x 'echo abcccde | tr -ds c c'

	# -c complements only the first string
	atf_check -o inline:'' -x 'echo abcde | tr -c -ds x y'
	atf_check -o inline:'c' -x 'echo abcde | tr -c -ds c x'
	atf_check -o inline:'' -x 'echo abcde | tr -c -ds x c'
	atf_check -o inline:'c' -x 'echo abcde | tr -c -ds c c'
	atf_check -o inline:'ccc' -x 'echo abcccde | tr -c -ds c x'
	atf_check -o inline:'' -x 'echo abcccde | tr -c -ds x c'
	atf_check -o inline:'c' -x 'echo abcccde | tr -c -ds c c'
}

#
# test substitution
#
atf_test_case subst
subst_head() {
	atf_set "descr" "Tests for tr substitution"
}

subst_body() {
	atf_check -o inline:'abcde\n' -x 'echo abcde | tr a-c a-c'
	atf_check -o inline:'cbade\n' -x 'echo abcde | tr a-c cba'
	atf_check -o inline:'abcde\n' -x 'echo abcde | tr a-z a-z'
	atf_check -o inline:'bcdef\n' -x 'echo abcde | tr a-z b-za'
	atf_check -o inline:'zabcd\n' -x 'echo abcde | tr b-za a-z'
	atf_check -o inline:'bbbbb\n' -x 'echo ababa | tr a b'
	atf_check -o inline:'furrfu\n' -x 'echo sheesh | tr a-z n-za-m'
	atf_check -o inline:'furrfu\n' -x 'echo sheesh | tr n-za-m a-z'

	atf_check -o inline:'ABCDE\n' -x 'echo abcde | tr a-z A-Z'
	atf_check -o inline:'ABC\n' \
	    -x 'echo abc | tr '"'[:lower:]' '[:upper:]'"

	# If you don't give enough substitution chars the last is repeated.
	atf_check -o inline:'bozoo\n' -x 'echo abcde | tr a-z bozo'
	atf_check -o inline:'qaaaa\n' -x 'echo abcde | tr a-z qa'

	# You can use -s with substitution.
	atf_check -o inline:'cbade\n' -x 'echo abcde | tr -s a-c cba'
	atf_check -o inline:'cbaddee\n' -x 'echo aabbccddee | tr -s a-c cba'
}

#
# test substitution with -c (does not currently work)
#
atf_test_case csubst
csubst_head() {
	atf_set "descr" "Tests for tr substitution with -c"
}

csubst_body() {
	atf_check -o inline:'abcde\n' -x \
	    'echo abcde | tr -c '"'\0-ac-\377' b"
	atf_check -o inline:'abcde\n' -x \
	    'echo abcde | tr -c '"'\0-ad-\377' bc"
	atf_check -o inline:'QUACK\n' -x \
	    'echo ABCDE | tr -c '"'\0-@' QUACK"
}

atf_init_test_cases() {
	atf_add_test_case dopt
	atf_add_test_case sopt
	atf_add_test_case dsopt
	atf_add_test_case subst
	atf_add_test_case csubst
}
