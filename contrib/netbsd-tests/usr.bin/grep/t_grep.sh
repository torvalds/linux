# $NetBSD: t_grep.sh,v 1.3 2017/01/14 20:43:52 christos Exp $
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

atf_test_case basic
basic_head()
{
	atf_set "descr" "Checks basic functionality"
}
basic_body()
{ 
	atf_check -o file:"$(atf_get_srcdir)/d_basic.out" -x \
	    'jot 10000 | grep 123'
}

atf_test_case binary
binary_head()
{
	atf_set "descr" "Checks handling of binary files"
}
binary_body()
{
	dd if=/dev/zero count=1 of=test.file
	echo -n "foobar" >> test.file
	atf_check -o file:"$(atf_get_srcdir)/d_binary.out" grep foobar test.file
}

atf_test_case recurse
recurse_head()
{
	atf_set "descr" "Checks recursive searching"
}
recurse_body()
{
	mkdir -p recurse/a/f recurse/d
	echo -e "cod\ndover sole\nhaddock\nhalibut\npilchard" > recurse/d/fish
	echo -e "cod\nhaddock\nplaice" > recurse/a/f/favourite-fish

	atf_check -o file:"$(atf_get_srcdir)/d_recurse.out" -x "grep -r haddock recurse | sort"
}

atf_test_case recurse_symlink
recurse_symlink_head()
{
	atf_set "descr" "Checks symbolic link recursion"
}
recurse_symlink_body()
{
	# Begin FreeBSD
	grep_type
	if [ $? -eq $GREP_TYPE_GNU ]; then
		atf_expect_fail "this test doesn't pass with gnu grep from ports"
	fi
	# End FreeBSD
	mkdir -p test/c/d
	(cd test/c/d && ln -s ../d .)
	echo "Test string" > test/c/match

	atf_check -o file:"$(atf_get_srcdir)/d_recurse_symlink.out" \
	    -e file:"$(atf_get_srcdir)/d_recurse_symlink.err" \
	    grep -r string test
}

atf_test_case word_regexps
word_regexps_head()
{
	atf_set "descr" "Checks word-regexps"
}
word_regexps_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_word_regexps.out" \
	    grep -w separated $(atf_get_srcdir)/d_input

	# Begin FreeBSD
	printf "xmatch pmatch\n" > test1

	atf_check -o inline:"pmatch\n" grep -Eow "(match )?pmatch" test1
	# End FreeBSD
}

atf_test_case begin_end
begin_end_head()
{
	atf_set "descr" "Checks handling of line beginnings and ends"
}
begin_end_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_begin_end_a.out" \
	    grep ^Front "$(atf_get_srcdir)/d_input"

	atf_check -o file:"$(atf_get_srcdir)/d_begin_end_b.out" \
	    grep ending$ "$(atf_get_srcdir)/d_input"
}

atf_test_case ignore_case
ignore_case_head()
{
	atf_set "descr" "Checks ignore-case option"
}
ignore_case_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_ignore_case.out" \
	    grep -i Upper "$(atf_get_srcdir)/d_input"
}

atf_test_case invert
invert_head()
{
	atf_set "descr" "Checks selecting non-matching lines with -v option"
}
invert_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_invert.out" \
	    grep -v fish "$(atf_get_srcdir)/d_invert.in"
}

atf_test_case whole_line
whole_line_head()
{
	atf_set "descr" "Checks whole-line matching with -x flag"
}
whole_line_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_whole_line.out" \
	    grep -x matchme "$(atf_get_srcdir)/d_input"
}

atf_test_case negative
negative_head()
{
	atf_set "descr" "Checks handling of files with no matches"
}
negative_body()
{
	atf_check -s ne:0 grep "not a hope in hell" "$(atf_get_srcdir)/d_input"
}

atf_test_case context
context_head()
{
	atf_set "descr" "Checks displaying context with -A, -B and -C flags"
}
context_body()
{
	cp $(atf_get_srcdir)/d_context_*.* .

	atf_check -o file:d_context_a.out grep -C2 bamboo d_context_a.in
	atf_check -o file:d_context_b.out grep -A3 tilt d_context_a.in
	atf_check -o file:d_context_c.out grep -B4 Whig d_context_a.in
	atf_check -o file:d_context_d.out grep -C1 pig d_context_a.in d_context_b.in
	atf_check -o file:d_context_e.out \
	    grep -E -C1 '(banana|monkey)' d_context_e.in
	atf_check -o file:d_context_f.out \
	    grep -Ev -B2 '(banana|monkey|fruit)' d_context_e.in
	atf_check -o file:d_context_g.out \
	    grep -Ev -A1 '(banana|monkey|fruit)' d_context_e.in
}

atf_test_case file_exp
file_exp_head()
{
	atf_set "descr" "Checks reading expressions from file"
}
file_exp_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_file_exp.out" -x \
	    'jot 21 -1 1.00 | grep -f '"$(atf_get_srcdir)"'/d_file_exp.in'
}

atf_test_case egrep
egrep_head()
{
	atf_set "descr" "Checks matching special characters with egrep"
}
egrep_body()
{
	atf_check -o file:"$(atf_get_srcdir)/d_egrep.out" \
		egrep '\?|\*$$' "$(atf_get_srcdir)/d_input"
}

atf_test_case zgrep
zgrep_head()
{
	atf_set "descr" "Checks handling of gzipped files with zgrep"
}
zgrep_body()
{
	cp "$(atf_get_srcdir)/d_input" .
	gzip d_input || atf_fail "gzip failed"

	atf_check -o file:"$(atf_get_srcdir)/d_zgrep.out" zgrep -h line d_input.gz
}

atf_test_case nonexistent
nonexistent_head()
{
	atf_set "descr" "Checks that -s flag suppresses error" \
	                "messages about nonexistent files"
}
nonexistent_body()
{
	atf_check -s ne:0 grep -s foobar nonexistent
}

atf_test_case context2
context2_head()
{
	atf_set "descr" "Checks displaying context with -z flag"
}
context2_body()
{
	printf "haddock\000cod\000plaice\000" > test1
	printf "mackeral\000cod\000crab\000" > test2

	atf_check -o file:"$(atf_get_srcdir)/d_context2_a.out" \
	    grep -z -A1 cod test1 test2

	atf_check -o file:"$(atf_get_srcdir)/d_context2_b.out" \
	    grep -z -B1 cod test1 test2

	atf_check -o file:"$(atf_get_srcdir)/d_context2_c.out" \
	    grep -z -C1 cod test1 test2
}
# Begin FreeBSD

# What grep(1) are we working with?
# - 0 : bsdgrep
# - 1 : gnu grep 2.51 (base)
# - 2 : gnu grep (ports)
GREP_TYPE_BSD=0
GREP_TYPE_GNU_FREEBSD=1
GREP_TYPE_GNU=2
GREP_TYPE_UNKNOWN=3

grep_type()
{
	local grep_version=$(grep --version)

	case "$grep_version" in
	*"BSD grep"*)
		return $GREP_TYPE_BSD
		;;
	*"GNU grep"*)
		case "$grep_version" in
		*2.5.1-FreeBSD*)
			return $GREP_TYPE_GNU_FREEBSD
			;;
		*)
			return $GREP_TYPE_GNU
			;;
		esac
		;;
	esac
	atf_fail "unknown grep type: $grep_version"
}

atf_test_case oflag_zerolen
oflag_zerolen_head()
{
	atf_set "descr" "Check behavior of zero-length matches with -o flag (PR 195763)"
}
oflag_zerolen_body()
{
	grep_type
	if [ $? -eq $GREP_TYPE_GNU_FREEBSD ]; then
		atf_expect_fail "this test doesn't pass with gnu grep in base"
	fi

	atf_check -o file:"$(atf_get_srcdir)/d_oflag_zerolen_a.out" \
	    grep -Eo '(^|:)0*' "$(atf_get_srcdir)/d_oflag_zerolen_a.in"

	atf_check -o file:"$(atf_get_srcdir)/d_oflag_zerolen_b.out" \
	    grep -Eo '(^|:)0*' "$(atf_get_srcdir)/d_oflag_zerolen_b.in"

	atf_check -o file:"$(atf_get_srcdir)/d_oflag_zerolen_c.out" \
	    grep -Eo '[[:alnum:]]*' "$(atf_get_srcdir)/d_oflag_zerolen_c.in"

	atf_check -o empty grep -Eo '' "$(atf_get_srcdir)/d_oflag_zerolen_d.in"

	atf_check -o file:"$(atf_get_srcdir)/d_oflag_zerolen_e.out" \
	    grep -o -e 'ab' -e 'bc' "$(atf_get_srcdir)/d_oflag_zerolen_e.in"

	atf_check -o file:"$(atf_get_srcdir)/d_oflag_zerolen_e.out" \
	    grep -o -e 'bc' -e 'ab' "$(atf_get_srcdir)/d_oflag_zerolen_e.in"
}

atf_test_case xflag
xflag_head()
{
	atf_set "descr" "Check that we actually get a match with -x flag (PR 180990)"
}
xflag_body()
{
	echo 128 > match_file
	seq 1 128 > pattern_file
	grep -xf pattern_file match_file
}

atf_test_case color
color_head()
{
	atf_set "descr" "Check --color support"
}
color_body()
{
	grep_type
	if [ $? -eq $GREP_TYPE_GNU_FREEBSD ]; then
		atf_expect_fail "this test doesn't pass with gnu grep in base"
	fi

	echo 'abcd*' > grepfile
	echo 'abc$' >> grepfile
	echo '^abc' >> grepfile

	atf_check -o file:"$(atf_get_srcdir)/d_color_a.out" \
	    grep --color=auto -e '.*' -e 'a' "$(atf_get_srcdir)/d_color_a.in"

	atf_check -o file:"$(atf_get_srcdir)/d_color_b.out" \
	    grep --color=auto -f grepfile "$(atf_get_srcdir)/d_color_b.in"

	atf_check -o file:"$(atf_get_srcdir)/d_color_c.out" \
	    grep --color=always -f grepfile "$(atf_get_srcdir)/d_color_b.in"
}

atf_test_case f_file_empty
f_file_empty_head()
{
	atf_set "descr" "Check for handling of a null byte in empty file, specified by -f (PR 202022)"
}
f_file_empty_body()
{
	printf "\0\n" > nulpat

	atf_check -s exit:1 grep -f nulpat "$(atf_get_srcdir)/d_f_file_empty.in"
}

atf_test_case escmap
escmap_head()
{
	atf_set "descr" "Check proper handling of escaped vs. unescaped dot expressions (PR 175314)"
}
escmap_body()
{
	atf_check -s exit:1 grep -o 'f.o\.' "$(atf_get_srcdir)/d_escmap.in"
	atf_check -o not-empty grep -o 'f.o.' "$(atf_get_srcdir)/d_escmap.in"
}

atf_test_case egrep_empty_invalid
egrep_empty_invalid_head()
{
	atf_set "descr" "Check for handling of an invalid empty pattern (PR 194823)"
}
egrep_empty_invalid_body()
{
	atf_check -e ignore -s not-exit:0 egrep '{' /dev/null
}

atf_test_case zerolen
zerolen_head()
{
	atf_set "descr" "Check for successful zero-length matches with ^$"
}
zerolen_body()
{
	printf "Eggs\n\nCheese" > test1

	atf_check -o inline:"\n" grep -e "^$" test1

	atf_check -o inline:"Eggs\nCheese\n" grep -v -e "^$" test1
}

atf_test_case wflag_emptypat
wflag_emptypat_head()
{
	atf_set "descr" "Check for proper handling of -w with an empty pattern (PR 105221)"
}
wflag_emptypat_body()
{
	printf "" > test1
	printf "\n" > test2
	printf "qaz" > test3
	printf " qaz\n" > test4

	atf_check -s exit:1 -o empty grep -w -e "" test1

	atf_check -o file:test2 grep -w -e "" test2

	atf_check -s exit:1 -o empty grep -w -e "" test3

	atf_check -o file:test4 grep -w -e "" test4
}

atf_test_case excessive_matches
excessive_matches_head()
{
	atf_set "descr" "Check for proper handling of lines with excessive matches (PR 218811)"
}
excessive_matches_body()
{
	grep_type
	if [ $? -eq $GREP_TYPE_GNU_FREEBSD ]; then
		atf_expect_fail "this test does not pass with GNU grep in base"
	fi

	for i in $(jot 4096); do
		printf "x" >> test.in
	done

	atf_check -s exit:0 -x '[ $(grep -o x test.in | wc -l) -eq 4096 ]'
	atf_check -s exit:1 -x 'grep -on x test.in | grep -v "1:x"'
}

atf_test_case fgrep_sanity
fgrep_sanity_head()
{
	atf_set "descr" "Check for fgrep sanity, literal expressions only"
}
fgrep_sanity_body()
{
	printf "Foo" > test1

	atf_check -o inline:"Foo\n" fgrep -e "Foo" test1

	atf_check -s exit:1 -o empty fgrep -e "Fo." test1
}

atf_test_case egrep_sanity
egrep_sanity_head()
{
	atf_set "descr" "Check for egrep sanity, EREs only"
}
egrep_sanity_body()
{
	printf "Foobar(ed)" > test1
	printf "M{1}" > test2

	atf_check -o inline:"Foo\n" egrep -o -e "F.." test1

	atf_check -o inline:"Foobar\n" egrep -o -e "F[a-z]*" test1

	atf_check -o inline:"Fo\n" egrep -o -e "F(o|p)" test1

	atf_check -o inline:"(ed)\n" egrep -o -e "\(ed\)" test1

	atf_check -o inline:"M\n" egrep -o -e "M{1}" test2

	atf_check -o inline:"M{1}\n" egrep -o -e "M\{1\}" test2
}

atf_test_case grep_sanity
grep_sanity_head()
{
	atf_set "descr" "Check for basic grep sanity, BREs only"
}
grep_sanity_body()
{
	printf "Foobar(ed)" > test1
	printf "M{1}" > test2

	atf_check -o inline:"Foo\n" grep -o -e "F.." test1

	atf_check -o inline:"Foobar\n" grep -o -e "F[a-z]*" test1

	atf_check -o inline:"Fo\n" grep -o -e "F\(o\)" test1

	atf_check -o inline:"(ed)\n" grep -o -e "(ed)" test1

	atf_check -o inline:"M{1}\n" grep -o -e "M{1}" test2

	atf_check -o inline:"M\n" grep -o -e "M\{1\}" test2
}

atf_test_case wv_combo_break
wv_combo_break_head()
{
	atf_set "descr" "Check for incorrectly matching lines with both -w and -v flags (PR 218467)"
}
wv_combo_break_body()
{
	printf "x xx\n" > test1
	printf "xx x\n" > test2

	atf_check -o file:test1 grep -w "x" test1
	atf_check -o file:test2 grep -w "x" test2

	atf_check -s exit:1 grep -v -w "x" test1
	atf_check -s exit:1 grep -v -w "x" test2
}

atf_test_case ocolor_metadata
ocolor_metadata_head()
{
	atf_set "descr" "Check for -n/-b producing per-line metadata output"
}
ocolor_metadata_body()
{
	grep_type
	if [ $? -eq $GREP_TYPE_GNU_FREEBSD ]; then
		atf_expect_fail "this test does not pass with GNU grep in base"
	fi

	printf "xxx\nyyyy\nzzz\nfoobarbaz\n" > test1
	check_expr="^[^:]*[0-9][^:]*:[^:]+$"

	atf_check -o inline:"1:1:xx\n" grep -bon "xx$" test1

	atf_check -o inline:"2:4:yyyy\n" grep -bn "yy" test1

	atf_check -o inline:"2:6:yy\n" grep -bon "yy$" test1

	# These checks ensure that grep isn't producing bogus line numbering
	# in the middle of a line.
	atf_check -s exit:1 -x \
	    "grep -Eon 'x|y|z|f' test1 | grep -Ev '${check_expr}'"

	atf_check -s exit:1 -x \
	    "grep -En 'x|y|z|f' --color=always test1 | grep -Ev '${check_expr}'"

	atf_check -s exit:1 -x \
	    "grep -Eon 'x|y|z|f' --color=always test1 | grep -Ev '${check_expr}'"
}

atf_test_case grep_nomatch_flags
grep_nomatch_flags_head()
{
	atf_set "descr" "Check for no match (-c, -l, -L, -q) flags not producing line matches or context (PR 219077)"
}

grep_nomatch_flags_body()
{
	printf "A\nB\nC\n" > test1

	atf_check -o inline:"1\n" grep -c -C 1 -e "B" test1
	atf_check -o inline:"1\n" grep -c -B 1 -e "B" test1
	atf_check -o inline:"1\n" grep -c -A 1 -e "B" test1
	atf_check -o inline:"1\n" grep -c -C 1 -e "B" test1

	atf_check -o inline:"test1\n" grep -l -e "B" test1
	atf_check -o inline:"test1\n" grep -l -B 1 -e "B" test1
	atf_check -o inline:"test1\n" grep -l -A 1 -e "B" test1
	atf_check -o inline:"test1\n" grep -l -C 1 -e "B" test1

	atf_check -s exit:1 -o inline:"test1\n" grep -L -e "D" test1

	atf_check -o empty grep -q -e "B" test1
	atf_check -o empty grep -q -B 1 -e "B" test1
	atf_check -o empty grep -q -A 1 -e "B" test1
	atf_check -o empty grep -q -C 1 -e "B" test1
}

atf_test_case badcontext
badcontext_head()
{
	atf_set "descr" "Check for handling of invalid context arguments"
}
badcontext_body()
{
	printf "A\nB\nC\n" > test1

	atf_check -s not-exit:0 -e ignore grep -A "-1" "B" test1

	atf_check -s not-exit:0 -e ignore grep -B "-1" "B" test1

	atf_check -s not-exit:0 -e ignore grep -C "-1" "B" test1

	atf_check -s not-exit:0 -e ignore grep -A "B" "B" test1

	atf_check -s not-exit:0 -e ignore grep -B "B" "B" test1

	atf_check -s not-exit:0 -e ignore grep -C "B" "B" test1
}

atf_test_case binary_flags
binary_flags_head()
{
	atf_set "descr" "Check output for binary flags (-a, -I, -U, --binary-files)"
}
binary_flags_body()
{
	printf "A\000B\000C" > test1
	printf "A\n\000B\n\000C" > test2
	binmatchtext="Binary file test1 matches\n"

	# Binaries not treated as text (default, -U)
	atf_check -o inline:"${binmatchtext}" grep 'B' test1
	atf_check -o inline:"${binmatchtext}" grep 'B' -C 1 test1

	atf_check -o inline:"${binmatchtext}" grep -U 'B' test1
	atf_check -o inline:"${binmatchtext}" grep -U 'B' -C 1 test1

	# Binary, -a, no newlines
	atf_check -o inline:"A\000B\000C\n" grep -a 'B' test1
	atf_check -o inline:"A\000B\000C\n" grep -a 'B' -C 1 test1

	# Binary, -a, newlines
	atf_check -o inline:"\000B\n" grep -a 'B' test2
	atf_check -o inline:"A\n\000B\n\000C\n" grep -a 'B' -C 1 test2

	# Binary files ignored
	atf_check -s exit:1 grep -I 'B' test2

	# --binary-files equivalence
	atf_check -o inline:"${binmatchtext}" grep --binary-files=binary 'B' test1
	atf_check -o inline:"A\000B\000C\n" grep --binary-files=text 'B' test1
	atf_check -s exit:1 grep --binary-files=without-match 'B' test2
}

atf_test_case mmap
mmap_head()
{
	atf_set "descr" "Check basic matching with --mmap flag"
}
mmap_body()
{
	grep_type
	if [ $? -eq $GREP_TYPE_GNU ]; then
		atf_expect_fail "gnu grep from ports has no --mmap option"
	fi

	printf "A\nB\nC\n" > test1

	atf_check -s exit:0 -o inline:"B\n" grep --mmap -oe "B" test1
	atf_check -s exit:1 grep --mmap -e "Z" test1
}

atf_test_case matchall
matchall_head()
{
	atf_set "descr" "Check proper behavior of matching all with an empty string"
}
matchall_body()
{
	printf "" > test1
	printf "A" > test2
	printf "A\nB" > test3

	atf_check -o inline:"test2:A\ntest3:A\ntest3:B\n" grep "" test1 test2 test3
	atf_check -o inline:"test3:A\ntest3:B\ntest2:A\n" grep "" test3 test1 test2
	atf_check -o inline:"test2:A\ntest3:A\ntest3:B\n" grep "" test2 test3 test1

	atf_check -s exit:1 grep "" test1
}

atf_test_case fgrep_multipattern
fgrep_multipattern_head()
{
	atf_set "descr" "Check proper behavior with multiple patterns supplied to fgrep"
}
fgrep_multipattern_body()
{
	printf "Foo\nBar\nBaz" > test1

	atf_check -o inline:"Foo\nBaz\n" grep -F -e "Foo" -e "Baz" test1
	atf_check -o inline:"Foo\nBaz\n" grep -F -e "Baz" -e "Foo" test1
	atf_check -o inline:"Bar\nBaz\n" grep -F -e "Bar" -e "Baz" test1
}

atf_test_case fgrep_icase
fgrep_icase_head()
{
	atf_set "descr" "Check proper handling of -i supplied to fgrep"
}
fgrep_icase_body()
{
	printf "Foo\nBar\nBaz" > test1

	atf_check -o inline:"Foo\nBaz\n" grep -Fi -e "foo" -e "baz" test1
	atf_check -o inline:"Foo\nBaz\n" grep -Fi -e "baz" -e "foo" test1
	atf_check -o inline:"Bar\nBaz\n" grep -Fi -e "bar" -e "baz" test1
	atf_check -o inline:"Bar\nBaz\n" grep -Fi -e "BAR" -e "bAz" test1
}

atf_test_case fgrep_oflag
fgrep_oflag_head()
{
	atf_set "descr" "Check proper handling of -o supplied to fgrep"
}
fgrep_oflag_body()
{
	printf "abcdefghi\n" > test1

	atf_check -o inline:"a\n" grep -Fo "a" test1
	atf_check -o inline:"i\n" grep -Fo "i" test1
	atf_check -o inline:"abc\n" grep -Fo "abc" test1
	atf_check -o inline:"fgh\n" grep -Fo "fgh" test1
	atf_check -o inline:"cde\n" grep -Fo "cde" test1
	atf_check -o inline:"bcd\n" grep -Fo -e "bcd" -e "cde" test1
	atf_check -o inline:"bcd\nefg\n" grep -Fo -e "bcd" -e "efg" test1

	atf_check -s exit:1 grep -Fo "xabc" test1
	atf_check -s exit:1 grep -Fo "abcx" test1
	atf_check -s exit:1 grep -Fo "xghi" test1
	atf_check -s exit:1 grep -Fo "ghix" test1
	atf_check -s exit:1 grep -Fo "abcdefghiklmnopqrstuvwxyz" test1
}

atf_test_case cflag
cflag_head()
{
	atf_set "descr" "Check proper handling of -c"
}
cflag_body()
{
	printf "a\nb\nc\n" > test1

	atf_check -o inline:"1\n" grep -Ec "a" test1
	atf_check -o inline:"2\n" grep -Ec "a|b" test1
	atf_check -o inline:"3\n" grep -Ec "a|b|c" test1

	atf_check -o inline:"test1:2\n" grep -EHc "a|b" test1
}

atf_test_case mflag
mflag_head()
{
	atf_set "descr" "Check proper handling of -m"
}
mflag_body()
{
	printf "a\nb\nc\nd\ne\nf\n" > test1

	atf_check -o inline:"1\n" grep -m 1 -Ec "a" test1
	atf_check -o inline:"2\n" grep -m 2 -Ec "a|b" test1
	atf_check -o inline:"3\n" grep -m 3 -Ec "a|b|c|f" test1

	atf_check -o inline:"test1:2\n" grep -m 2 -EHc "a|b|e|f" test1
}
# End FreeBSD

atf_init_test_cases()
{
	atf_add_test_case basic
	atf_add_test_case binary
	atf_add_test_case recurse
	atf_add_test_case recurse_symlink
	atf_add_test_case word_regexps
	atf_add_test_case begin_end
	atf_add_test_case ignore_case
	atf_add_test_case invert
	atf_add_test_case whole_line
	atf_add_test_case negative
	atf_add_test_case context
	atf_add_test_case file_exp
	atf_add_test_case egrep
	atf_add_test_case zgrep
	atf_add_test_case nonexistent
	atf_add_test_case context2
# Begin FreeBSD
	atf_add_test_case oflag_zerolen
	atf_add_test_case xflag
	atf_add_test_case color
	atf_add_test_case f_file_empty
	atf_add_test_case escmap
	atf_add_test_case egrep_empty_invalid
	atf_add_test_case zerolen
	atf_add_test_case wflag_emptypat
	atf_add_test_case excessive_matches
	atf_add_test_case wv_combo_break
	atf_add_test_case fgrep_sanity
	atf_add_test_case egrep_sanity
	atf_add_test_case grep_sanity
	atf_add_test_case ocolor_metadata
	atf_add_test_case grep_nomatch_flags
	atf_add_test_case binary_flags
	atf_add_test_case badcontext
	atf_add_test_case mmap
	atf_add_test_case matchall
	atf_add_test_case fgrep_multipattern
	atf_add_test_case fgrep_icase
	atf_add_test_case fgrep_oflag
	atf_add_test_case cflag
	atf_add_test_case mflag
# End FreeBSD
}
