# $NetBSD: t_here.sh,v 1.6 2016/03/31 16:21:52 christos Exp $
#
# Copyright (c) 2007 The NetBSD Foundation, Inc.
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

nl='
'

reset()
{
	TEST_NUM=0
	TEST_FAILURES=''
	TEST_FAIL_COUNT=0
	TEST_ID="$1"
}

check()
{
	fail=false
	TEMP_FILE=$( mktemp OUT.XXXXXX )
	TEST_NUM=$(( $TEST_NUM + 1 ))

	# our local shell (ATF_SHELL) better do quoting correctly...
	# some of the tests expect us to expand $nl internally...
	CMD="nl='${nl}'; $1"

	result="$( ${TEST_SH} -c "${CMD}" 2>"${TEMP_FILE}" )"
	STATUS=$?

	if [ "${STATUS}" -ne "$3" ]; then
		echo >&2 "[$TEST_NUM] expected exit code $3, got ${STATUS}"

		# don't actually fail just because of wrong exit code
		# unless we either expected, or received "good"
		case "$3/${STATUS}" in
		(*/0|0/*) fail=true;;
		esac
	fi

	if [ "$3" -eq 0 ]; then
		if [ -s "${TEMP_FILE}" ]; then
			echo >&2 \
			 "[$TEST_NUM] Messages produced on stderr unexpected..."
			cat "${TEMP_FILE}" >&2
			fail=true
		fi
	else
		if ! [ -s "${TEMP_FILE}" ]; then
			echo >&2 \
		    "[$TEST_NUM] Expected messages on stderr, nothing produced"
			fail=true
		fi
	fi
	rm -f "${TEMP_FILE}"

	# Remove newlines (use local shell for this)
	oifs="$IFS"
	IFS="$nl"
	result="$(echo $result)"
	IFS="$oifs"
	if [ "$2" != "$result" ]
	then
		echo >&2 "[$TEST_NUM] Expected output '$2', received '$result'"
		fail=true
	fi

	if $fail
	then
		echo >&2 "[$TEST_NUM] Full command: <<${CMD}>>"
	fi

	$fail && test -n "$TEST_ID" && {
		TEST_FAILURES="${TEST_FAILURES}${TEST_FAILURES:+
}${TEST_ID}[$TEST_NUM]: test of '$1' failed";
		TEST_FAIL_COUNT=$(( $TEST_FAIL_COUNT + 1 ))
		return 0
	}
	$fail && atf_fail "Test[$TEST_NUM] of '$1' failed"
	return 0
}

results()
{
	test -z "${TEST_ID}" && return 0
	test -z "${TEST_FAILURES}" && return 0

	echo >&2 "=========================================="
	echo >&2 "While testing '${TEST_ID}'"
	echo >&2 " - - - - - - - - - - - - - - - - -"
	echo >&2 "${TEST_FAILURES}"
	atf_fail \
 "Test ${TEST_ID}: $TEST_FAIL_COUNT subtests (of $TEST_NUM) failed - see stderr"
}

atf_test_case do_simple
do_simple_head() {
	atf_set "descr" "Basic tests for here documents"
}
do_simple_body() {
	y=x

	reset 'simple'
	IFS=' 	'
	check 'x=`cat <<EOF'$nl'text'${nl}EOF$nl'`; echo $x' 'text' 0
	check 'x=`cat <<\EOF'$nl'text'${nl}EOF$nl'`; echo $x' 'text' 0

	check "y=${y};"'x=`cat <<EOF'$nl'te${y}t'${nl}EOF$nl'`; echo $x' \
			'text' 0
	check "y=${y};"'x=`cat <<\EOF'$nl'te${y}t'${nl}EOF$nl'`; echo $x'  \
			'te${y}t' 0
	check "y=${y};"'x=`cat <<"EOF"'$nl'te${y}t'${nl}EOF$nl'`; echo $x'  \
			'te${y}t' 0
	check "y=${y};"'x=`cat <<'"'EOF'"$nl'te${y}t'${nl}EOF$nl'`; echo $x'  \
			'te${y}t' 0

	# check that quotes in the here doc survive and cause no problems
	check "cat <<EOF${nl}te'xt${nl}EOF$nl" "te'xt" 0
	check "cat <<\EOF${nl}te'xt${nl}EOF$nl" "te'xt" 0
	check "cat <<'EOF'${nl}te'xt${nl}EOF$nl" "te'xt" 0
	check "cat <<EOF${nl}te\"xt${nl}EOF$nl" 'te"xt' 0
	check "cat <<\EOF${nl}te\"xt${nl}EOF$nl" 'te"xt' 0
	check "cat <<'EOF'${nl}te\"xt${nl}EOF$nl" 'te"xt' 0
	check "cat <<'EO'F${nl}te\"xt${nl}EOF$nl" 'te"xt' 0

	check "y=${y};"'x=`cat <<EOF'$nl'te'"'"'${y}t'${nl}EOF$nl'`; echo $x' \
			'te'"'"'xt' 0
	check "y=${y};"'x=`cat <<EOF'$nl'te'"''"'${y}t'${nl}EOF$nl'`; echo $x' \
			'te'"''"'xt' 0

	# note that the blocks of empty space in the following must
	# be entirely tab characters, no spaces.

	check 'x=`cat <<EOF'"$nl	text${nl}EOF$nl"'`; echo "$x"' \
			'	text' 0
	check 'x=`cat <<-EOF'"$nl	text${nl}EOF$nl"'`; echo $x' \
			'text' 0
	check 'x=`cat <<-EOF'"${nl}text${nl}	EOF$nl"'`; echo $x' \
			'text' 0
	check 'x=`cat <<-\EOF'"$nl	text${nl}	EOF$nl"'`; echo $x' \
			'text' 0
	check 'x=`cat <<- "EOF"'"$nl	text${nl}EOF$nl"'`; echo $x' \
			'text' 0
	check 'x=`cat <<- '"'EOF'${nl}text${nl}	EOF$nl"'`; echo $x' \
			'text' 0
	results
}

atf_test_case end_markers
end_markers_head() {
	atf_set "descr" "Tests for various end markers of here documents"
}
end_markers_body() {

	reset 'end_markers'
	for end in EOF 1 \! '$$$' "string " a\\\ a\\\ \   '&' '' ' ' '  ' \
	    --STRING-- . '~~~' ')' '(' '#' '()' '(\)' '(\/)' '--' '\' '{' '}' \
VERYVERYVERYVERYLONGLONGLONGin_fact_absurdly_LONG_LONG_HERE_DOCUMENT_TERMINATING_MARKER_THAT_goes_On_forever_and_ever_and_ever...
	do
		# check unquoted end markers
		case "${end}" in
		('' | *[' ()\$&#*~']* ) ;;	# skip unquoted endmark test for these
		(*)	check \
	'x=$(cat << '"${end}${nl}text${nl}${end}${nl}"'); printf %s "$x"' 'text' 0
			;;
		esac

		# and quoted end markers
		check \
	'x=$(cat <<'"'${end}'${nl}text${nl}${end}${nl}"'); printf %s "$x"' 'text' 0

		# and see what happens if we encounter "almost" an end marker
		case "${#end}" in
		(0|1)	;;		# too short to try truncation tests
		(*)	check \
   'x=$(cat <<'"'${end}'${nl}text${nl}${end%?}${nl}${end}${nl}"'); printf %s "$x"' \
				"text ${end%?}" 0
			check \
   'x=$(cat <<'"'${end}'${nl}text${nl}${end#?}${nl}${end}${nl}"'); printf %s "$x"' \
				"text ${end#?}" 0
			check \
   'x=$(cat <<'"'${end}'${nl}text${nl}${end%?}+${nl}${end}${nl}"');printf %s "$x"' \
				"text ${end%?}+" 0
			;;
		esac

		# or something that is a little longer
		check \
   'x=$(cat <<'"'${end}'${nl}text${nl}${end}x${nl}${end}${nl}"'); printf %s "$x"' \
				"text ${end}x" 0
		check \
    'x=$(cat <<'"'${end}'${nl}text${nl}!${end}${nl}${end}${nl}"'); printf %s "$x"' \
				"text !${end}" 0

		# or which does not begin at start of line
		check \
    'x=$(cat <<'"'${end}'${nl}text${nl} ${end}${nl}${end}${nl}"'); printf %s "$x"' \
				"text  ${end}" 0
		check \
    'x=$(cat <<'"'${end}'${nl}text${nl}	${end}${nl}${end}${nl}"'); printf %s "$x"' \
				"text 	${end}" 0

		# or end at end of line
		check \
    'x=$(cat <<'"'${end}'${nl}text${nl}${end} ${nl}${end}${nl}"'); printf %s "$x"' \
				"text ${end} " 0

		# or something that is correct much of the way, but then...

		case "${#end}" in
		(0)	;;		# cannot test this one
		(1)	check \
    'x=$(cat <<'"'${end}'${nl}text${nl}${end}${end}${nl}${end}${nl}"'); printf %s "$x"' \
				"text ${end}${end}" 0
			;;
		(2-7)	pfx="${end%?}"
			check \
    'x=$(cat <<'"'${end}'${nl}text${nl}${end}${pfx}${nl}${end}${nl}"'); printf %s "$x"' \
				"text ${end}${pfx}" 0
			check \
    'x=$(cat <<'"'${end}'${nl}text${nl}${pfx}${end}${nl}${end}${nl}"'); printf %s "$x"' \
				"text ${pfx}${end}" 0
			;;
		(*)	pfx=${end%??????}; sfx=${end#??????}
			check \
    'x=$(cat <<'"'${end}'${nl}text${nl}${end}${sfx}${nl}${end}${nl}"'); printf %s "$x"' \
				"text ${end}${sfx}" 0
			check \
    'x=$(cat <<'"'${end}'${nl}text${nl}${pfx}${end}${nl}${end}${nl}"'); printf %s "$x"' \
				"text ${pfx}${end}" 0
			check \
    'x=$(cat <<'"'${end}'${nl}text${nl}${pfx}${sfx}${nl}${end}${nl}"'); printf %s "$x"' \
				"text ${pfx}${sfx}" 0
			;;
		esac
	done

	# Add striptabs tests (in similar way) here one day...

	results
}

atf_test_case incomplete
incomplete_head() {
	atf_set "descr" "Basic tests for incomplete here documents"
}
incomplete_body() {
	reset incomplete

	check 'cat <<EOF' '' 2
	check 'cat <<- EOF' '' 2
	check 'cat <<\EOF' '' 2
	check 'cat <<- \EOF' '' 2

	check 'cat <<EOF'"${nl}" '' 2
	check 'cat <<- EOF'"${nl}" '' 2
	check 'cat <<'"'EOF'${nl}" '' 2
	check 'cat <<- "EOF"'"${nl}" '' 2

	check 'cat << EOF'"${nl}${nl}" '' 2
	check 'cat <<-EOF'"${nl}${nl}" '' 2
	check 'cat << '"'EOF'${nl}${nl}" '' 2
	check 'cat <<-"EOF"'"${nl}${nl}" '' 2

	check 'cat << EOF'"${nl}"'line 1'"${nl}" '' 2
	check 'cat <<-EOF'"${nl}"'	line 1'"${nl}" '' 2
	check 'cat << EOF'"${nl}"'line 1'"${nl}"'	line 2'"${nl}" '' 2
	check 'cat <<-EOF'"${nl}"'	line 1'"${nl}"'line 2'"${nl}" '' 2

	check 'cat << EOF'"${nl}line 1${nl}${nl}line3${nl}${nl}5!${nl}" '' 2

	results
}

atf_test_case lineends
lineends_head() {
	atf_set "descr" "Tests for line endings in here documents"
}
lineends_body() {
	reset lineends

	# note that "check" removes newlines from stdout before comparing.
	# (they become blanks, provided there is something before & after)

	check 'cat << \echo'"${nl}"'\'"${nl}echo${nl}echo${nl}" '\' 0
	check 'cat <<  echo'"${nl}"'\'"${nl}echo${nl}echo${nl}" 'echo' 0
	check 'cat << echo'"${nl}"'\\'"${nl}echo${nl}echo${nl}" '\' 0

	check 'X=3; cat << ec\ho'"${nl}"'$X\'"${nl}echo${nl}echo${nl}" \
		'$X\'  0
	check 'X=3; cat <<  echo'"${nl}"'$X'"${nl}echo${nl}echo${nl}" \
		'3'  0
	check 'X=3; cat <<  echo'"${nl}"'$X\'"${nl}echo${nl}echo${nl}" \
		''  0
	check 'X=3; cat <<  echo'"${nl}"'${X}\'"${nl}echo${nl}echo${nl}" \
		'3echo'  0
	check 'X=3; cat <<  echo'"${nl}"'\$X\'"${nl}echo${nl}echo${nl}" \
		'$Xecho'  0
	check 'X=3; cat <<  echo'"${nl}"'\\$X \'"${nl}echo${nl}echo${nl}" \
		'\3 echo'  0

	check \
  'cat << "echo"'"${nl}"'line1\'"${nl}"'line2\'"${nl}echo${nl}echo${nl}" \
		 'line1\ line2\'  0
	check \
	  'cat << echo'"${nl}"'line1\'"${nl}"'line2\'"${nl}echo${nl}echo${nl}" \
	  'line1line2echo'  0

	results
}

atf_test_case multiple
multiple_head() {
	atf_set "descr" "Tests for multiple here documents on one cmd line"
}
multiple_body() {
	reset multiple

	check \
    "(cat ; cat <&3) <<EOF0 3<<EOF3${nl}STDIN${nl}EOF0${nl}-3-${nl}EOF3${nl}" \
		'STDIN -3-' 0

	check "(read line; echo \"\$line\"; cat <<EOF1; echo \"\$line\") <<EOF2
The File
EOF1
The Line
EOF2
"			'The Line The File The Line' 0

	check "(read line; echo \"\$line\"; cat <<EOF; echo \"\$line\") <<EOF
The File
EOF
The Line
EOF
"			'The Line The File The Line' 0

	check "V=1; W=2; cat <<-1; cat <<2; cat <<- 3; cat <<'4';"' cat <<\5
		$V
		$W
		3
	4
	5
			1
2
	5
					4*$W+\$V
	3
$W
1
2
3
4
7+$V
$W+6
5
'			'1 2 3 4 5 5 4*2+$V $W 1 2 3 7+$V $W+6'	0

	results
}

atf_test_case nested
nested_head() {
	atf_set "descr" "Tests for nested here documents for one cmd"
}
nested_body() {
	reset nested

	check \
'cat << EOF1'"${nl}"'$(cat << EOF2'"${nl}LINE${nl}EOF2${nl}"')'"${nl}EOF1${nl}"\
	'LINE' 0

# This next one fails ... and correctly, so we will omit it (bad test)
# Reasoning is that the correct data "$(cat << EOF2)\nLINE\nEOF2\n" is
# collected for the outer (EOF1) heredoc, when that is parsed, it looks
# like
#	$(cat <<EOF2)
#	LINE
#	EOF2
# which looks like a good command - except it is being parsed in "heredoc"
# syntax, which means it is enclosed in double quotes, which means that
# the newline after the ')' in the first line is not a newline token, but
# just a character.  The EOF2 heredoc cannot start until after the next
# newline token, of which there are none here...  LINE and EOF2 are just
# more data in the outer EOF1 heredoc for its "cat" command to read & write.
#
# The previous sub-test works because there the \n comes inside the
# $( ), and in there, the outside quoting rules are suspended, and it
# all starts again - so that \n is a newline token, and the EOF2 heredoc
# is processed.
#
#	check \
#   'cat << EOF1'"${nl}"'$(cat << EOF2 )'"${nl}LINE${nl}EOF2${nl}EOF1${nl}" \
#	'LINE' 0

	L='cat << EOF1'"${nl}"'LINE1$(cat << EOF2'"${nl}"
	L="${L}"'LINE2$(cat << EOF3'"${nl}"
	L="${L}"'LINE3$(cat << EOF4'"${nl}"
	L="${L}"'LINE4$(cat << EOF5'"${nl}"
	L="${L}LINE5${nl}EOF5${nl})4${nl}EOF4${nl})3${nl}"
	L="${L}EOF3${nl})2${nl}EOF2${nl})1${nl}EOF1${nl}"

	# That mess is ...
	#
	#	cat <<EOF1
	#	LINE1$(cat << EOF2
	#	LINE2$(cat << EOF3
	#	LINE3$(cat << EOF4
	#	LINE4$(cat << EOF5
	#	LINE5
	#	EOF5
	#	)4
	#	EOF4
	#	)3
	#	EOF3
	#	)2
	#	EOF2
	#	)1
	#	EOF1

	check "${L}" 'LINE1LINE2LINE3LINE4LINE54321' 0

	results
}

atf_test_case quoting
quoting_head() {
	atf_set "descr" "Tests for use of quotes inside here documents"
}
quoting_body() {
	reset quoting

	check 'X=!; cat <<- E\0F
		<'\''"'\'' \\$X\$X  "'\''" \\>
	E0F
	'	'<'\''"'\'' \\$X\$X  "'\''" \\>'	0

	check 'X=!; cat <<- E0F
		<'\''"'\'' \\$X\$X  "'\''" \\>
	E0F
	'	'<'\''"'\'' \!$X  "'\''" \>'	0

	check 'cat <<- END
		$( echo "'\''" ) $( echo '\''"'\'' ) $( echo \\ )
	END
	'	"' \" \\"		0

	check 'X=12345; Y="string1 line1?-line2"; Z=; unset W; cat <<-EOF
		${#X}${Z:-${Y}}${W+junk}${Y%%l*}${Y#*\?}
		"$Z"'\''$W'\'' ${Y%" "*} $(( X + 54321 ))
	EOF
	'	'5string1 line1?-line2string1 -line2 ""'\'\'' string1 66666' 0

	results
}

atf_test_case side_effects
side_effects_head() {
	atf_set "descr" "Tests how side effects in here documents are handled"
}
side_effects_body() {

	atf_check -s exit:0 -o inline:'2\n1\n' -e empty ${TEST_SH} -c '
		unset X
		cat <<-EOF
		${X=2}
		EOF
		echo "${X-1}"
		'
}

atf_test_case vicious
vicious_head() {
	atf_set "descr" "Tests for obscure and obnoxious uses of here docs"
}
vicious_body() {
	reset

	cat <<- \END_SCRIPT > script
		cat <<ONE && cat \
		<<TWO
		a
		ONE
		b
		TWO
	END_SCRIPT

	atf_check -s exit:0 -o inline:'a\nb\n' -e empty ${TEST_SH} script

	# This next one is causing discussion currently (late Feb 2016)
	# amongst stds writers & implementors.   Consequently we
	# will not check what it produces.   The eventual result
	# seems unlikely to be what we currently output, which
	# is:
	#	A:echo line 1
	#	B:echo line 2)" && prefix DASH_CODE <<DASH_CODE
	#	B:echo line 3
	#	line 4
	#	line 5
	#
	# The likely intended output is ...
	#
	#	A:echo line 3
	#	B:echo line 1
	#	line 2
	#	DASH_CODE:echo line 4)"
	#	DASH_CODE:echo line 5
	#
	# The difference is explained by differing opinions on just
	# when processing of a here doc should start

	cat <<- \END_SCRIPT > script
		prefix() { sed -e "s/^/$1:/"; }
		DASH_CODE() { :; }

		prefix A <<XXX && echo "$(prefix B <<XXX
		echo line 1
		XXX
		echo line 2)" && prefix DASH_CODE <<DASH_CODE
		echo line 3
		XXX
		echo line 4)"
		echo line 5
		DASH_CODE
	END_SCRIPT

	# we will just verify that the shell can parse the
	# script somehow, and doesn't fall over completely...

	atf_check -s exit:0 -o ignore -e empty ${TEST_SH} script
}

atf_init_test_cases() {
	atf_add_test_case do_simple	# not worthy of a comment
	atf_add_test_case end_markers	# the mundane, the weird, the bizarre
	atf_add_test_case incomplete	# where the end marker isn't...
	atf_add_test_case lineends	# test weird line endings in heredocs
	atf_add_test_case multiple	# multiple << operators on one cmd
	atf_add_test_case nested	# here docs inside here docs
	atf_add_test_case quoting	# stuff quoted inside
	atf_add_test_case side_effects	# here docs that modify environment
	atf_add_test_case vicious	# evil test from the austin-l list...
}
