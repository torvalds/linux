# $NetBSD: t_cmdsub.sh,v 1.4 2016/04/04 12:40:13 christos Exp $
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

#
# This file tests command substitutions ( `...` and $( ... ) )
#
# CAUTION:
#	Be careful attempting running these tests outside the ATF environment
#	Some of the tests run "rm *" in the current directory to clean up
#	An ATF test directory should be empty already, outside ATF, anything

atf_test_case a_basic_cmdsub
a_basic_cmdsub_head() {
	atf_set "descr" 'Test operation of simple $( ) substitutions'
}
a_basic_cmdsub_body() {
	atf_check -s exit:0 -o match:'Result is true today' -e empty \
	    ${TEST_SH} -c \
		'echo Result is $( true && echo true || echo false ) today'

	atf_check -s exit:0 -o match:'Result is false today' -e empty \
	    ${TEST_SH} -c \
		'echo Result is $( false && echo true || echo false ) today'

	atf_check -s exit:0 -o match:'aaabbbccc' -e empty \
	    ${TEST_SH} -c 'echo aaa$( echo bbb )ccc'
	atf_check -s exit:0 -o match:'aaabbb cccddd' -e empty \
	    ${TEST_SH} -c 'echo aaa$( echo bbb ccc )ddd'
	atf_check -s exit:0 -o inline:'aaabbb cccddd\n' -e empty \
	    ${TEST_SH} -c 'echo aaa$( echo bbb; echo ccc )ddd'
	atf_check -s exit:0 -o inline:'aaabbb\ncccddd\n' -e empty \
	    ${TEST_SH} -c 'echo "aaa$( echo bbb; echo ccc )ddd"'

	atf_check -s exit:0 -o inline:'some string\n' -e empty \
	    ${TEST_SH} -c 'X=$( echo some string ); echo "$X"'
	atf_check -s exit:0 -o inline:'weird; string *\n' -e empty \
	    ${TEST_SH} -c 'X=$( echo "weird; string *" ); echo "$X"'

	rm -f * 2>/dev/null || :
	for f in file-1 file-2
	do
		cp /dev/null "$f"
	done

	atf_check -s exit:0 -o match:'Found file-1 file-2' -e empty \
	    ${TEST_SH} -c 'echo Found $( echo * )'
	atf_check -s exit:0 -o match:'Found file-1 file-2' -e empty \
	    ${TEST_SH} -c 'echo Found "$( echo * )"'
	atf_check -s exit:0 -o match:'Found file-1 file-2' -e empty \
	    ${TEST_SH} -c 'echo Found $('" echo '*' )"
	atf_check -s exit:0 -o match:'Found \*' -e empty \
	    ${TEST_SH} -c 'echo Found "$('" echo '*' "')"'
	atf_check -s exit:0 -o match:'Found file-1 file-2' -e empty \
	    ${TEST_SH} -c 'echo Found $('" echo \\* )"
	atf_check -s exit:0 -o match:'Found \*' -e empty \
	    ${TEST_SH} -c 'echo Found "$('" echo \\* )"\"
}

atf_test_case b_basic_backticks
b_basic_backticks_head() {
	atf_set "descr" 'Test operation of old style ` ` substitutions'
}
b_basic_backticks_body() {
	atf_check -s exit:0 -o match:'Result is true today' -e empty \
	    ${TEST_SH} -c \
		'echo Result is `true && echo true || echo false` today'

	atf_check -s exit:0 -o match:'Result is false today' -e empty \
	    ${TEST_SH} -c \
		'echo Result is `false && echo true || echo false` today'

	atf_check -s exit:0 -o match:'aaabbbccc' -e empty \
	    ${TEST_SH} -c 'echo aaa` echo bbb `ccc'
	atf_check -s exit:0 -o match:'aaabbb cccddd' -e empty \
	    ${TEST_SH} -c 'echo aaa` echo bbb ccc `ddd'
	atf_check -s exit:0 -o inline:'aaabbb cccddd\n' -e empty \
	    ${TEST_SH} -c 'echo aaa` echo bbb; echo ccc `ddd'
	atf_check -s exit:0 -o inline:'aaabbb\ncccddd\n' -e empty \
	    ${TEST_SH} -c 'echo "aaa` echo bbb; echo ccc `ddd"'

	atf_check -s exit:0 -o inline:'some string\n' -e empty \
	    ${TEST_SH} -c 'X=` echo some string `; echo "$X"'
	atf_check -s exit:0 -o inline:'weird; string *\n' -e empty \
	    ${TEST_SH} -c 'X=` echo "weird; string *" `; echo "$X"'

	rm -f * 2>/dev/null || :
	for f in file-1 file-2
	do
		cp /dev/null "$f"
	done

	atf_check -s exit:0 -o match:'Found file-1 file-2' -e empty \
	    ${TEST_SH} -c 'echo Found ` echo * `'
	atf_check -s exit:0 -o match:'Found file-1 file-2' -e empty \
	    ${TEST_SH} -c 'echo Found "` echo * `"'
	atf_check -s exit:0 -o match:'Found file-1 file-2' -e empty \
	    ${TEST_SH} -c 'echo Found `'" echo '*' "'`'
	atf_check -s exit:0 -o match:'Found \*' -e empty \
	    ${TEST_SH} -c 'echo Found "`'" echo '*' "'`"'
	atf_check -s exit:0 -o match:'Found file-1 file-2' -e empty \
	    ${TEST_SH} -c 'echo Found `'" echo \\* "'`'
	atf_check -s exit:0 -o match:'Found \*' -e empty \
	    ${TEST_SH} -c 'echo Found "`'" echo \\* "'`"'
}

atf_test_case c_nested_cmdsub
c_nested_cmdsub_head() {
	atf_set "descr" "Test that cmd substitutions can be nested"
}
c_nested_cmdsub_body() {
	atf_check -s exit:0 -o match:'__foobarbletch__' -e empty \
	    ${TEST_SH} -c 'echo __$( echo foo$(echo bar)bletch )__'
	atf_check -s exit:0 -o match:'_abcde_' -e empty \
	    ${TEST_SH} -c 'echo _$(echo a$(echo $(echo b)c$(echo d))e )_'
	atf_check -s exit:0 -o match:'123454321' -e empty \
	    ${TEST_SH} -c 'echo 1$(echo 2$(echo 3$(echo 4$(echo 5)4)3)2)1'
}

atf_test_case d_nested_backticks
d_nested_backticks_head() {
	atf_set "descr" "Tests that old style backtick cmd subs can be nested"
}
d_nested_backticks_body() {
	atf_check -s exit:0 -o match:'__foobarbletch__' -e empty \
	    ${TEST_SH} -c 'echo __` echo foo\`echo bar\`bletch `__'
	atf_check -s exit:0 -o match:'_abcde_' -e empty \
	    ${TEST_SH} -c \
		'echo _`echo a\`echo \\\`echo b\\\`c\\\`echo d\\\`\`e `_'
	atf_check -s exit:0 -o match:'123454321' -e empty \
	    ${TEST_SH} -c \
	    'echo 1`echo 2\`echo 3\\\`echo 4\\\\\\\`echo 5\\\\\\\`4\\\`3\`2`1'
}

atf_test_case e_perverse_mixing
e_perverse_mixing_head() {
	atf_set "descr" \
		"Checks various mixed new and old style cmd substitutions"
}
e_perverse_mixing_body() {
	atf_check -s exit:0 -o match:'__foobarbletch__' -e empty \
	    ${TEST_SH} -c 'echo __$( echo foo`echo bar`bletch )__'
	atf_check -s exit:0 -o match:'__foobarbletch__' -e empty \
	    ${TEST_SH} -c 'echo __` echo foo$(echo bar)bletch `__'
	atf_check -s exit:0 -o match:'_abcde_' -e empty \
	    ${TEST_SH} -c 'echo _$(echo a`echo $(echo b)c$(echo d)`e )_'
	atf_check -s exit:0 -o match:'_abcde_' -e empty \
	    ${TEST_SH} -c 'echo _`echo a$(echo \`echo b\`c\`echo d\`)e `_'
	atf_check -s exit:0 -o match:'12345654321' -e empty \
	    ${TEST_SH} -c \
		'echo 1`echo 2$(echo 3\`echo 4\\\`echo 5$(echo 6)5\\\`4\`3)2`1'
}

atf_test_case f_redirect_in_cmdsub
f_redirect_in_cmdsub_head() {
	atf_set "descr" "Checks that redirects work in command substitutions"
}
f_redirect_in_cmdsub_body() {
	atf_require_prog cat
	atf_require_prog rm

	rm -f file 2>/dev/null || :
	atf_check -s exit:0 -o match:'_aa_' -e empty \
	    ${TEST_SH} -c 'echo _$( echo a$( echo b > file )a)_'
	atf_check -s exit:0 -o match:b -e empty ${TEST_SH} -c 'cat file'
	atf_check -s exit:0 -o match:'_aba_' -e empty \
	    ${TEST_SH} -c 'echo _$( echo a$( cat < file )a)_'
	atf_check -s exit:0 -o match:'_aa_' -e empty \
	    ${TEST_SH} -c 'echo _$( echo a$( echo d >> file )a)_'
	atf_check -s exit:0 -o inline:'b\nd\n' -e empty ${TEST_SH} -c 'cat file'
	atf_check -s exit:0 -o match:'_aa_' -e match:'not error' \
	    ${TEST_SH} -c 'echo _$( echo a$( echo not error >&2 )a)_'
}

atf_test_case g_redirect_in_backticks
g_redirect_in_backticks_head() {
	atf_set "descr" "Checks that redirects work in old style cmd sub"
}
g_redirect_in_backticks_body() {
	atf_require_prog cat
	atf_require_prog rm

	rm -f file 2>/dev/null || :
	atf_check -s exit:0 -o match:'_aa_' -e empty \
	    ${TEST_SH} -c 'echo _` echo a\` echo b > file \`a`_'
	atf_check -s exit:0 -o match:b -e empty ${TEST_SH} -c 'cat file'
	atf_check -s exit:0 -o match:'_aba_' -e empty \
	    ${TEST_SH} -c 'echo _` echo a\` cat < file \`a`_'
	atf_check -s exit:0 -o match:'_aa_' -e empty \
	    ${TEST_SH} -c 'echo _` echo a\` echo d >> file \`a`_'
	atf_check -s exit:0 -o inline:'b\nd\n' -e empty ${TEST_SH} -c 'cat file'
	atf_check -s exit:0 -o match:'_aa_' -e match:'not error' \
	    ${TEST_SH} -c 'echo _` echo a\` echo not error >&2 \`a`_'
}

atf_test_case h_vars_in_cmdsub
h_vars_in_cmdsub_head() {
	atf_set "descr" "Check that variables work in command substitutions"
}
h_vars_in_cmdsub_body() {
	atf_check -s exit:0 -o match:'__abc__' -e empty \
	    ${TEST_SH} -c 'X=abc; echo __$( echo ${X} )__'
	atf_check -s exit:0 -o match:'__abc__' -e empty \
	    ${TEST_SH} -c 'X=abc; echo __$( echo "${X}" )__'
	atf_check -s exit:0 -o match:'__abc__' -e empty \
	    ${TEST_SH} -c 'X=abc; echo "__$( echo ${X} )__"'
	atf_check -s exit:0 -o match:'__abc__' -e empty \
	    ${TEST_SH} -c 'X=abc; echo "__$( echo "${X}" )__"'

	atf_check -s exit:0 -o inline:'a\n\nb\n\nc\n' -e empty \
	    ${TEST_SH} -c "for X in a '' b '' c"'; do echo $( echo "$X" ); done'

	atf_check -s exit:0 -o match:'__acd__' -e empty \
	    ${TEST_SH} -c 'X=; unset Y; echo "__$( echo a${X-b}${Y-c}d)__"'
	atf_check -s exit:0 -o match:'__abcd__' -e empty \
	    ${TEST_SH} -c 'X=; unset Y; echo "__$( echo a${X:-b}${Y:-c}d)__"'
	atf_check -s exit:0 -o match:'__XYX__' -e empty \
	    ${TEST_SH} -c 'X=X; echo "__${X}$( X=Y; echo ${X} )${X}__"'
	atf_check -s exit:0 -o match:'__def__' -e empty \
	    ${TEST_SH} -c 'X=abc; echo "__$(X=def; echo "${X}" )__"'
	atf_check -s exit:0 -o inline:'abcdef\nabc\n' -e empty \
	    ${TEST_SH} -c 'X=abc; echo "$X$(X=def; echo ${X} )"; echo $X'
}

atf_test_case i_vars_in_backticks
i_vars_in_backticks_head() {
	atf_set "descr" "Checks that variables work in old style cmd sub"
}
i_vars_in_backticks_body() {
	atf_check -s exit:0 -o match:'__abc__' -e empty \
	    ${TEST_SH} -c 'X=abc; echo __` echo ${X} `__'
	atf_check -s exit:0 -o match:'__abc__' -e empty \
	    ${TEST_SH} -c 'X=abc; echo __` echo "${X}" `__'
	atf_check -s exit:0 -o match:'__abc__' -e empty \
	    ${TEST_SH} -c 'X=abc; echo "__` echo ${X} `__"'
	atf_check -s exit:0 -o match:'__abc__' -e empty \
	    ${TEST_SH} -c 'X=abc; echo "__` echo \"${X}\" `__"'

	atf_check -s exit:0 -o inline:'a\n\nb\n\nc\n' -e empty \
	    ${TEST_SH} -c "for X in a '' b '' c"'; do echo $( echo "$X" ); done'

	atf_check -s exit:0 -o match:'__acd__' -e empty \
	    ${TEST_SH} -c 'X=; unset Y; echo "__$( echo a${X-b}${Y-c}d)__"'
	atf_check -s exit:0 -o match:'__abcd__' -e empty \
	    ${TEST_SH} -c 'X=; unset Y; echo "__$( echo a${X:-b}${Y:-c}d)__"'
	atf_check -s exit:0 -o match:'__XYX__' -e empty \
	    ${TEST_SH} -c 'X=X; echo "__${X}$( X=Y; echo ${X} )${X}__"'
	atf_check -s exit:0 -o inline:'abcdef\nabc\n' -e empty \
	    ${TEST_SH} -c 'X=abc; echo "$X`X=def; echo \"${X}\" `";echo $X'

	# The following is nonsense, so is not included ...
	# atf_check -s exit:0 -o match:'__abc__' -e empty \
	#                              oV             cV   oV   cV
	#    ${TEST_SH} -c 'X=abc; echo "__`X=def echo "${X}" `__"'
	#				   `start in " ^ " ends, ` not yet
}

atf_test_case j_cmdsub_in_varexpand
j_cmdsub_in_varexpand_head() {
	atf_set "descr" "Checks that command sub can be used in var expansion"
}
j_cmdsub_in_varexpand_body() {
	atf_check -s exit:0 -o match:'foo' -e empty \
	    ${TEST_SH} -c 'X=set; echo ${X+$(echo foo)}'
	atf_check -s exit:0 -o match:'set' -e empty \
	    ${TEST_SH} -c 'X=set; echo ${X-$(echo foo)}'
	rm -f bar 2>/dev/null || :
	atf_check -s exit:0 -o match:'set' -e empty \
	    ${TEST_SH} -c 'X=set; echo ${X-$(echo foo > bar)}'
	test -f bar && atf_fail "bar should not exist, but does"
	atf_check -s exit:0 -o inline:'\n' -e empty \
	    ${TEST_SH} -c 'X=set; echo ${X+$(echo foo > bar)}'
	test -f bar || atf_fail "bar should exist, but does not"
}

atf_test_case k_backticks_in_varexpand
k_backticks_in_varexpand_head() {
	atf_set "descr" "Checks that old style cmd sub works in var expansion"
}
k_backticks_in_varexpand_body() {
	atf_check -s exit:0 -o match:'foo' -e empty \
	    ${TEST_SH} -c 'X=set; echo ${X+`echo foo`}'
	atf_check -s exit:0 -o match:'set' -e empty \
	    ${TEST_SH} -c 'X=set; echo ${X-`echo foo`}'
	rm -f bar 2>/dev/null || :
	atf_check -s exit:0 -o match:'set' -e empty \
	    ${TEST_SH} -c 'X=set; echo ${X-`echo foo > bar`}'
	test -f bar && atf_fail "bar should not exist, but does"
	atf_check -s exit:0 -o inline:'\n' -e empty \
	    ${TEST_SH} -c 'X=set; echo ${X+`echo foo > bar`}'
	test -f bar || atf_fail "bar should exist, but does not"
}

atf_test_case l_arithmetic_in_cmdsub
l_arithmetic_in_cmdsub_head() {
	atf_set "descr" "Checks that arithmetic works in cmd substitutions"
}
l_arithmetic_in_cmdsub_body() {
	atf_check -s exit:0 -o inline:'1 + 1 = 2\n' -e empty \
	    ${TEST_SH} -c 'echo 1 + 1 = $( echo $(( 1 + 1 )) )'
	atf_check -s exit:0 -o inline:'X * Y = 6\n' -e empty \
	    ${TEST_SH} -c 'X=2; Y=3; echo X \* Y = $( echo $(( X * Y )) )'
	atf_check -s exit:0 -o inline:'Y % X = 1\n' -e empty \
	    ${TEST_SH} -c 'X=2; Y=3; echo Y % X = $( echo $(( $Y % $X )) )'
}

atf_test_case m_arithmetic_in_backticks
m_arithmetic_in_backticks_head() {
	atf_set "descr" "Checks that arithmetic works in old style cmd sub"
}
m_arithmetic_in_backticks_body() {
	atf_check -s exit:0 -o inline:'2 + 3 = 5\n' -e empty \
	    ${TEST_SH} -c 'echo 2 + 3 = ` echo $(( 2 + 3 )) `'
	atf_check -s exit:0 -o inline:'X * Y = 6\n' -e empty \
	    ${TEST_SH} -c 'X=2; Y=3; echo X \* Y = ` echo $(( X * Y )) `'
	atf_check -s exit:0 -o inline:'Y % X = 1\n' -e empty \
	    ${TEST_SH} -c 'X=2; Y=3; echo Y % X = ` echo $(( $Y % $X )) `'
}

atf_test_case n_cmdsub_in_arithmetic
n_cmdsub_in_arithmetic_head() {
	atf_set "descr" "Tests uses of command substitutions in arithmetic"
}
n_cmdsub_in_arithmetic_body() {
	atf_check -s exit:0 -o inline:'7\n' -e empty \
	    ${TEST_SH} -c 'echo $(( $( echo 3 ) $( echo + ) $( echo 4 ) ))'
	atf_check -s exit:0 -o inline:'11\n7\n18\n4\n1\n' -e empty \
	    ${TEST_SH} -c \
		 'for op in + - \* / %
		  do
		      echo $(( $( echo 9 ) $( echo "${op}" ) $( echo 2 ) ))
		  done'
}

atf_test_case o_backticks_in_arithmetic
o_backticks_in_arithmetic_head() {
	atf_set "descr" "Tests old style cmd sub used in arithmetic"
}
o_backticks_in_arithmetic_body() {
	atf_check -s exit:0 -o inline:'33\n' -e empty \
	    ${TEST_SH} -c 'echo $(( `echo 77` `echo -` `echo 44`))'
	atf_check -s exit:0 -o inline:'14\n8\n33\n3\n2\n' -e empty \
	    ${TEST_SH} -c \
		 'for op in + - \* / %
		  do
		      echo $((`echo 11``echo "${op}"``echo 3`))
		  done'
}

atf_test_case p_cmdsub_in_heredoc
p_cmdsub_in_heredoc_head() {
	atf_set "descr" "Checks that cmdsubs work inside a here document"
}
p_cmdsub_in_heredoc_body() {
	atf_require_prog cat

	atf_check -s exit:0 -o inline:'line 1+1\nline 2\nline 3\n' -e empty \
	    ${TEST_SH} -c \
		'cat <<- EOF
			$( echo line 1 )$( echo +1 )
			$( echo line 2;echo line 3 )
		EOF'
}

atf_test_case q_backticks_in_heredoc
q_backticks_in_heredoc_head() {
	atf_set "descr" "Checks that old style cmdsubs work in here docs"
}
q_backticks_in_heredoc_body() {
	atf_require_prog cat

	atf_check -s exit:0 -o inline:'Mary had a\nlittle\nlamb\n' -e empty \
	    ${TEST_SH} -c \
		'cat <<- EOF
			`echo Mary ` `echo had a `
			` echo little; echo lamb `
		EOF'
}

atf_test_case r_heredoc_in_cmdsub
r_heredoc_in_cmdsub_head() {
	atf_set "descr" "Checks that here docs work inside cmd subs"
}
r_heredoc_in_cmdsub_body() {
	atf_require_prog cat

	atf_check -s exit:0 -o inline:'Mary had a\nlittle\nlamb\n' -e empty \
	    ${TEST_SH} -c 'echo "$( cat <<- \EOF
				Mary had a
				little
				lamb
			EOF
			)"'

	atf_check -s exit:0 -e empty \
	    -o inline:'Mary had 1\nlittle\nlamb\nMary had 4\nlittle\nlambs\n' \
	    ${TEST_SH} -c 'for N in 1 4; do echo "$( cat <<- EOF
				Mary had ${N}
				little
				lamb$( [ $N -gt 1 ] && echo s )
			EOF
			)"; done'


	atf_check -s exit:0 -o inline:'A Calculation:\n2 * 7 = 14\n' -e empty \
	    ${TEST_SH} -c 'echo "$( cat <<- EOF
				A Calculation:
					2 * 7 = $(( 2 * 7 ))
			EOF
			)"'
}

atf_test_case s_heredoc_in_backticks
s_heredoc_in_backticks_head() {
	atf_set "descr" "Checks that here docs work inside old style cmd subs"
}
s_heredoc_in_backticks_body() {
	atf_require_prog cat

	atf_check -s exit:0 -o inline:'Mary had a little lamb\n' -e empty \
	    ${TEST_SH} -c 'echo ` cat <<- \EOF
				Mary had a
				little
				lamb
			EOF
			`'

	atf_check -s exit:0 -o inline:'A Calculation:\n17 / 3 = 5\n' -e empty \
	    ${TEST_SH} -c 'echo "` cat <<- EOF
				A Calculation:
					17 / 3 = $(( 17 / 3 ))
			EOF
			`"'
}

atf_test_case t_nested_cmdsubs_in_heredoc
t_nested_cmdsubs_in_heredoc_head() {
	atf_set "descr" "Checks nested command substitutions in here docs"
}
t_nested_cmdsubs_in_heredoc_body() {
	atf_require_prog cat
	atf_require_prog rm

	rm -f * 2>/dev/null || :
	echo "Hello" > File

	atf_check -s exit:0 -o inline:'Hello U\nHelp me!\n' -e empty \
	    ${TEST_SH} -c 'cat <<- EOF
		$(cat File) U
		$( V=$(cat File); echo "${V%lo}p" ) me!
		EOF'

	rm -f * 2>/dev/null || :
	echo V>V ; echo A>A; echo R>R
	echo Value>VAR

	atf_check -s exit:0 -o inline:'$2.50\n' -e empty \
	    ${TEST_SH} -c 'cat <<- EOF
	$(Value='\''$2.50'\'';eval echo $(eval $(cat V)$(cat A)$(cat R)=\'\''\$$(cat $(cat V)$(cat A)$(cat R))\'\''; eval echo \$$(set -- *;echo ${3}${1}${2})))
		EOF'
}

atf_test_case u_nested_backticks_in_heredoc
u_nested_backticks_in_heredoc_head() {
	atf_set "descr" "Checks nested old style cmd subs in here docs"
}
u_nested_backticks_in_heredoc_body() {
	atf_require_prog cat
	atf_require_prog rm

	rm -f * 2>/dev/null || :
	echo "Hello" > File

	atf_check -s exit:0 -o inline:'Hello U\nHelp me!\n' -e empty \
	    ${TEST_SH} -c 'cat <<- EOF
		`cat File` U
		`V=\`cat File\`; echo "${V%lo}p" ` me!
		EOF'

	rm -f * 2>/dev/null || :
	echo V>V ; echo A>A; echo R>R
	echo Value>VAR

	atf_check -s exit:0 -o inline:'$5.20\n' -e empty \
	    ${TEST_SH} -c 'cat <<- EOF
	`Value='\''$5.20'\'';eval echo \`eval \\\`cat V\\\`\\\`cat A\\\`\\\`cat R\\\`=\\\'\''\\\$\\\`cat \\\\\\\`cat V\\\\\\\`\\\\\\\`cat A\\\\\\\`\\\\\\\`cat R\\\\\\\`\\\`\\\'\''; eval echo \\\$\\\`set -- *;echo \\\\\${3}\\\\\${1}\\\\\${2}\\\`\``
		EOF'
}

atf_test_case v_cmdsub_paren_tests
v_cmdsub__paren_tests_head() {
	atf_set "descr" "tests with cmdsubs containing embedded ')'"
}
v_cmdsub_paren_tests_body() {

	# Tests from:
	#	http://www.in-ulm.de/~mascheck/various/cmd-subst/
	# (slightly modified.)

	atf_check -s exit:0 -o inline:'A.1\n' -e empty ${TEST_SH} -c \
		'echo $(
			case x in  x) echo A.1;; esac
		)'

	atf_check -s exit:0 -o inline:'A.2\n' -e empty ${TEST_SH} -c \
		'echo $(
			case x in  x) echo A.2;; esac # comment
		)'

	atf_check -s exit:0 -o inline:'A.3\n' -e empty ${TEST_SH} -c \
		'echo $(
			case x in (x) echo A.3;; esac
		)'

	atf_check -s exit:0 -o inline:'A.4\n' -e empty ${TEST_SH} -c \
		'echo $(
			case x in (x) echo A.4;; esac # comment
		)'

	atf_check -s exit:0 -o inline:'A.5\n' -e empty ${TEST_SH} -c \
		'echo $(
			case x in (x) echo A.5
			esac
		)'

	atf_check -s exit:0 -o inline:'B: quoted )\n' -e empty ${TEST_SH} -c \
		'echo $(
			echo '\''B: quoted )'\''
		)'

	atf_check -s exit:0 -o inline:'C: comment then closing paren\n' \
		-e empty ${TEST_SH} -c \
			'echo $(
				echo C: comment then closing paren # )
			)'

	atf_check -s exit:0 -o inline:'D.1: here-doc with )\n' \
		-e empty ${TEST_SH} -c \
			'echo $(
				cat <<-\eof
				D.1: here-doc with )
				eof
			)'

	# D.2 is a bogus test.

	atf_check -s exit:0 -o inline:'D.3: here-doc with \()\n' \
		-e empty ${TEST_SH} -c \
			'echo $(
				cat <<-\eof
				D.3: here-doc with \()
				eof
			)'

	atf_check -s exit:0 -e empty \
	  -o inline:'E: here-doc terminated with a parenthesis ("academic")\n' \
		${TEST_SH} -c \
		'echo $(
			cat <<-\)
			E: here-doc terminated with a parenthesis ("academic")
			)
		)'

	atf_check -s exit:0 -e empty \
-o inline:'F.1: here-doc embed with unbal single, back- or doublequote '\''\n' \
		${TEST_SH} -c \
		'echo $(
			cat <<-"eof"
		F.1: here-doc embed with unbal single, back- or doublequote '\''
			eof
		)'
	atf_check -s exit:0 -e empty \
 -o inline:'F.2: here-doc embed with unbal single, back- or doublequote "\n' \
		${TEST_SH} -c \
		'echo $(
			cat <<-"eof"
		F.2: here-doc embed with unbal single, back- or doublequote "
			eof
		)'
	atf_check -s exit:0 -e empty \
 -o inline:'F.3: here-doc embed with unbal single, back- or doublequote `\n' \
		${TEST_SH} -c \
		'echo $(
			cat <<-"eof"
		F.3: here-doc embed with unbal single, back- or doublequote `
			eof
		)'

	atf_check -s exit:0 -e empty -o inline:'G: backslash at end of line\n' \
		${TEST_SH} -c \
			'echo $(
				echo G: backslash at end of line # \
			)'

	atf_check -s exit:0 -e empty \
		-o inline:'H: empty command-substitution\n' \
		${TEST_SH} -c 'echo H: empty command-substitution $( )'
}

atf_test_case w_heredoc_outside_cmdsub
w_heredoc_outside_cmdsub_head() {
	atf_set "descr" "Checks that here docs work inside cmd subs"
}
w_heredoc_outside_cmdsub_body() {
	atf_require_prog cat

	atf_check -s exit:0 -o inline:'Mary had a\nlittle\nlamb\n' -e empty \
	    ${TEST_SH} -c 'echo "$( cat <<- \EOF )"
				Mary had a
				little
				lamb
			EOF
			'

	atf_check -s exit:0 -e empty \
	    -o inline:'Mary had 1\nlittle\nlamb\nMary had 4\nlittle\nlambs\n' \
	    ${TEST_SH} -c 'for N in 1 4; do echo "$( cat <<- EOF )"
				Mary had ${N}
				little
				lamb$( [ $N -gt 1 ] && echo s )
			EOF
			done'


	atf_check -s exit:0 -o inline:'A Calculation:\n2 * 7 = 14\n' -e empty \
	    ${TEST_SH} -c 'echo "$( cat <<- EOF)"
				A Calculation:
					2 * 7 = $(( 2 * 7 ))
			EOF
			'
}

atf_test_case x_heredoc_outside_backticks
x_heredoc_outside_backticks_head() {
	atf_set "descr" "Checks that here docs work inside old style cmd subs"
}
x_heredoc_outside_backticks_body() {
	atf_require_prog cat

	atf_check -s exit:0 -o inline:'Mary had a little lamb\n' -e empty \
	    ${TEST_SH} -c 'echo ` cat <<- \EOF `
				Mary had a
				little
				lamb
			EOF
			'

	atf_check -s exit:0 -o inline:'A Calculation:\n17 / 3 = 5\n' -e empty \
	    ${TEST_SH} -c 'echo "` cat <<- EOF `"
				A Calculation:
					17 / 3 = $(( 17 / 3 ))
			EOF
			'
}

atf_test_case t_nested_cmdsubs_in_heredoc
t_nested_cmdsubs_in_heredoc_head() {
	atf_set "descr" "Checks nested command substitutions in here docs"
}
t_nested_cmdsubs_in_heredoc_body() {
	atf_require_prog cat
	atf_require_prog rm

	rm -f * 2>/dev/null || :
	echo "Hello" > File

	atf_check -s exit:0 -o inline:'Hello U\nHelp me!\n' -e empty \
	    ${TEST_SH} -c 'cat <<- EOF
		$(cat File) U
		$( V=$(cat File); echo "${V%lo}p" ) me!
		EOF'

	rm -f * 2>/dev/null || :
	echo V>V ; echo A>A; echo R>R
	echo Value>VAR

	atf_check -s exit:0 -o inline:'$2.50\n' -e empty \
	    ${TEST_SH} -c 'cat <<- EOF
	$(Value='\''$2.50'\'';eval echo $(eval $(cat V)$(cat A)$(cat R)=\'\''\$$(cat $(cat V)$(cat A)$(cat R))\'\''; eval echo \$$(set -- *;echo ${3}${1}${2})))
		EOF'
}

atf_test_case z_absurd_heredoc_cmdsub_combos
z_absurd_heredoc_cmdsub_combos_head() {
	atf_set "descr" "perverse and unusual cmd substitutions & more"
}
z_absurd_heredoc_cmdsub_combos_body() {

	echo "Help!" > help

	# This version works in NetBSD (& FreeBSD)'s sh (and most others)
	atf_check -s exit:0 -o inline:'Help!\nMe 2\n' -e empty ${TEST_SH} -c '
			cat <<- EOF
				$(
					cat <<- STOP
						$(
							cat `echo help`
						)
					STOP
				)
				$(
					cat <<- END 4<<-TRASH
						Me $(( 1 + 1 ))
					END
					This is unused noise!
					TRASH
				)
			EOF
		'

	# atf_expect_fail "PR bin/50993 - heredoc parsing done incorrectly"
	atf_check -s exit:0 -o inline:'Help!\nMe 2\n' -e empty ${TEST_SH} -c '
			cat <<- EOF
				$(
					cat << STOP
						$(
							cat `echo help`
						)
					STOP
				)
				$(
					cat <<- END 4<<TRASH
						Me $(( 1 + 1 ))
					END
					This is unused noise!
					TRASH
				)
			EOF
		'
}

atf_init_test_cases() {
	atf_add_test_case a_basic_cmdsub
	atf_add_test_case b_basic_backticks
	atf_add_test_case c_nested_cmdsub
	atf_add_test_case d_nested_backticks
	atf_add_test_case e_perverse_mixing
	atf_add_test_case f_redirect_in_cmdsub
	atf_add_test_case g_redirect_in_backticks
	atf_add_test_case h_vars_in_cmdsub
	atf_add_test_case i_vars_in_backticks
	atf_add_test_case j_cmdsub_in_varexpand
	atf_add_test_case k_backticks_in_varexpand
	atf_add_test_case l_arithmetic_in_cmdsub
	atf_add_test_case m_arithmetic_in_backticks
	atf_add_test_case n_cmdsub_in_arithmetic
	atf_add_test_case o_backticks_in_arithmetic
	atf_add_test_case p_cmdsub_in_heredoc
	atf_add_test_case q_backticks_in_heredoc
	atf_add_test_case r_heredoc_in_cmdsub
	atf_add_test_case s_heredoc_in_backticks
	atf_add_test_case t_nested_cmdsubs_in_heredoc
	atf_add_test_case u_nested_backticks_in_heredoc
	atf_add_test_case v_cmdsub_paren_tests
	atf_add_test_case w_heredoc_outside_cmdsub
	atf_add_test_case x_heredoc_outside_backticks
	atf_add_test_case z_absurd_heredoc_cmdsub_combos
}
