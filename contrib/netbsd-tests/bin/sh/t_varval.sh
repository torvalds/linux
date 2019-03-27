# $NetBSD: t_varval.sh,v 1.1 2016/03/16 15:49:19 christos Exp $
#
# Copyright (c) 2016 The NetBSD Foundation, Inc.
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
# the implementation of "sh" to test
: ${TEST_SH:="/bin/sh"}

# Test all kinds of weird values in various ways to use shell $... expansions

oneline()
{
	q="'"
	test $# -eq 4 && q=""

	v=$( printf '\\%3.3o' $(( $2 & 0xFF )) )
	printf "%s" "$1"
	if [ $2 != 39 ]; then
		printf "%sprefix${v}suffix%s" "$q" "$q"
	elif [ $# -ne 4 ]; then
		printf %s prefix\"\'\"suffix
	else
		printf %s prefix\'suffix
	fi
	printf "%s\n" "$3"
}

mkdata() {
	quote= pfx=
	while [ $# -gt 0 ]
	do
		case "$1" in
		--)	shift; break;;
		-q)	quote=no; shift; continue;;
		esac

		pfx="${pfx}${pfx:+ }${1}"
		shift
	done

	sfx=
	while [ $# -gt 0 ]
	do
		sfx="${sfx}${sfx:+ }${1}"
		shift
	done

	i=1		# '\0' is not expected to work, anywhere...
	while [ $i -lt 256 ]
	do
		oneline "${pfx}" "$i" "${sfx}" $quote
		i=$(( $i + 1 ))
	done
}

atf_test_case aaa
aaa_head() {
	atf_set "descr" "Check that this test has a hope of working. " \
		"Just give up on these tests if the aaa test fails".
}
aaa_body() {
	oneline "echo " 9 '' |
		atf_check -s exit:0 -o inline:'prefix\tsuffix\n' -e empty \
			${TEST_SH}

	oneline "VAR=" 65 '; echo "${#VAR}:${VAR}"' |
		atf_check -s exit:0 -o inline:'13:prefixAsuffix\n' -e empty \
			${TEST_SH}

	oneline "VAR=" 1 '; echo "${#VAR}:${VAR}"' |
		atf_check -s exit:0 -o inline:'13:prefixsuffix\n' -e empty \
			${TEST_SH}

	oneline "VAR=" 10 '; echo "${#VAR}:${VAR}"' |
		atf_check -s exit:0 -o inline:'13:prefix\nsuffix\n' -e empty \
			${TEST_SH}

	rm -f prefix* 2>/dev/null || :
	oneline "echo hello >" 45 "" |
		atf_check -s exit:0 -o empty -e empty ${TEST_SH}
	test -f "prefix-suffix" ||
		atf_fail "failed to create prefix-suffix (45)"
	test -s "prefix-suffix" ||
		atf_fail "no data in prefix-suffix (45)"
	test "$(cat prefix-suffix)" = "hello" ||
		atf_fail "incorrect data in prefix-suffix (45)"

	return 0
}

atf_test_case assignment
assignment_head() {
	atf_set "descr" "Check that all chars can be assigned to vars"
}
assignment_body() {
	atf_require_prog grep
	atf_require_prog rm

	rm -f results || :
	mkdata "VAR=" -- '; echo ${#VAR}' |
		atf_check -s exit:0 -o save:results -e empty ${TEST_SH}
	test -z $( grep -v "^13$" results ) ||
		atf_fail "Incorrect lengths: $(grep -nv '^13$' results)"

	return 0
}

atf_test_case cmdline
cmdline_head() {
	atf_set "descr" "Check vars containing all chars can be used"
}
cmdline_body() {
	atf_require_prog rm
	atf_require_prog wc

	rm -f results || :
	mkdata "VAR=" -- '; echo "${VAR}"' |
		atf_check -s exit:0 -o save:results -e empty ${TEST_SH}

	# 256 because one output line contains a \n ...
	test $( wc -l < results ) -eq 256 ||
		atf_fail "incorrect line count in results"
	test $(wc -c < results) -eq $(( 255 * 14 )) ||
		atf_fail "incorrect character count in results"

	return 0
}

atf_test_case redirect
redirect_head() {
	atf_set "descr" "Check vars containing all chars can be used"
}
redirect_body() {
	atf_require_prog ls
	atf_require_prog wc
	atf_require_prog rm
	atf_require_prog mkdir
	atf_require_prog rmdir

	nl='
'

	rm -f prefix* suffix || :

	mkdir prefix		# one of the files will be prefix/suffix
	mkdata "VAR=" -- '; echo "${VAR}" > "${VAR}"' |
		atf_check -s exit:0 -o empty -e empty ${TEST_SH}

	test -f "prefix/suffix" ||
		atf_fail "Failed to create file in subdirectory"
	test $( wc -l < "prefix/suffix" ) -eq 1 ||
		atf_fail "Not exactly one line in prefix/suffix file"

	atf_check -s exit:0 -o empty -e empty rm "prefix/suffix"
	atf_check -s exit:0 -o empty -e empty rmdir "prefix"

	test -f "prefix${nl}suffix" ||
		atf_fail "Failed to create file with newline in its name"
	test $( wc -l < "prefix${nl}suffix" ) -eq 2 ||
		atf_fail "NewLine file did not contain embedded newline"

	atf_check -s exit:0 -o empty -e empty rm "prefix${nl}suffix"

	# Now there should be 253 files left...
	test $( ls | wc -l ) -eq 253 ||
		atf_fail \
   "Did not create all expected files: wanted: 253, found ($( ls | wc -l ))"

	# and each of them should have a name that is 13 chars long (+ \n)
	test $( ls | wc -c ) -eq $(( 253 * 14 )) ||
		atf_fail "File names do not appear to be as expected"

	return 0
}

atf_test_case read
read_head() {
	atf_set "descr" "Check vars containing all chars can be used"
}
read_body() {
	atf_require_prog ls
	atf_require_prog wc
	atf_require_prog rm
	atf_require_prog mkdir
	atf_require_prog rmdir

	nl='
'

	rm -f prefix* suffix || :

	mkdir prefix		# one of the files will be prefix/suffix
	mkdata -q |
		atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c '
			while read -r VAR
			do
				# skip the mess made by embedded newline
				case "${VAR}" in
				(prefix | suffix)	continue;;
				esac
				echo "${VAR}" > "${VAR}"
			done'

	test -f "prefix/suffix" ||
		atf_fail "Failed to create file in subdirectory"
	test $( wc -l < "prefix/suffix" ) -eq 1 ||
		atf_fail "Not exactly one line in prefix/suffix file"

	atf_check -s exit:0 -o empty -e empty rm "prefix/suffix"
	atf_check -s exit:0 -o empty -e empty rmdir "prefix"

	# Now there should be 253 files left...
	test $( ls | wc -l ) -eq 253 ||
		atf_fail \
   "Did not create all expected files: wanted: 253, found ($( ls | wc -l ))"

	# and each of them should have a name that is 13 chars long (+ \n)
	test $( ls | wc -c ) -eq $(( 253 * 14 )) ||
		atf_fail "File names do not appear to be as expected"

	return 0
}

atf_init_test_cases() {
	atf_add_test_case aaa
	atf_add_test_case assignment
	atf_add_test_case cmdline
	atf_add_test_case redirect
	atf_add_test_case read
}
