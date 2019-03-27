# $NetBSD: t_redir.sh,v 1.9 2016/05/14 00:33:02 kre Exp $
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

# Any failures in this first test means it is not worth bothering looking
# for causes of failures in any other tests, make this one work first.

# Problems with this test usually mean inadequate ATF_SHELL used for testing.
# (though if all pass but the last, it might be a TEST_SH problem.)

atf_test_case basic_test_method_test
basic_test_method_test_head()
{
	atf_set "descr" "Tests that test method works as expected"
}
basic_test_method_test_body()
{
	cat <<- 'DONE' |
	DONE
	atf_check -s exit:0 -o empty -e empty ${TEST_SH}
	cat <<- 'DONE' |
	DONE
	atf_check -s exit:0 -o match:0 -e empty ${TEST_SH} -c 'wc -l'

	cat <<- 'DONE' |
		echo hello
	DONE
	atf_check -s exit:0 -o match:hello -e empty ${TEST_SH} 
	cat <<- 'DONE' |
		echo hello
	DONE
	atf_check -s exit:0 -o match:1 -e empty ${TEST_SH} -c 'wc -l'

	cat <<- 'DONE' |
		echo hello\
					world
	DONE
	atf_check -s exit:0 -o match:helloworld -e empty ${TEST_SH} 
	cat <<- 'DONE' |
		echo hello\
					world
	DONE
	atf_check -s exit:0 -o match:2 -e empty ${TEST_SH} -c 'wc -l'

	printf '%s\n%s\n%s\n' Line1 Line2 Line3 > File
	atf_check -s exit:0 -o inline:'Line1\nLine2\nLine3\n' -e empty \
		${TEST_SH} -c 'cat File'

	cat <<- 'DONE' |
		set -- X "" '' Y
		echo ARGS="${#}"
		echo '' -$1- -$2- -$3- -$4-
		cat <<EOF
			X=$1
		EOF
		cat <<\EOF
			Y=$4
		EOF
	DONE
	atf_check -s exit:0 -o match:ARGS=4 -o match:'-X- -- -- -Y-' \
		-o match:X=X -o match:'Y=\$4' -e empty ${TEST_SH} 
}

atf_test_case do_input_redirections
do_input_redirections_head()
{
	atf_set "descr" "Tests that simple input redirection works"
}
do_input_redirections_body()
{
	printf '%s\n%s\n%s\nEND\n' 'First Line' 'Second Line' 'Line 3' >File

	atf_check -s exit:0 -e empty \
		-o inline:'First Line\nSecond Line\nLine 3\nEND\n' \
		${TEST_SH} -c 'cat < File'
	atf_check -s exit:0 -e empty \
		-o inline:'First Line\nSecond Line\nLine 3\nEND\n' \
		${TEST_SH} -c 'cat <File'
	atf_check -s exit:0 -e empty \
		-o inline:'First Line\nSecond Line\nLine 3\nEND\n' \
		${TEST_SH} -c 'cat< File'
	atf_check -s exit:0 -e empty \
		-o inline:'First Line\nSecond Line\nLine 3\nEND\n' \
		${TEST_SH} -c 'cat < "File"'
	atf_check -s exit:0 -e empty \
		-o inline:'First Line\nSecond Line\nLine 3\nEND\n' \
		${TEST_SH} -c '< File cat'

	ln File wc
	atf_check -s exit:0 -e empty \
		-o inline:'First Line\nSecond Line\nLine 3\nEND\n' \
		${TEST_SH} -c '< wc cat'

	mv wc cat
	atf_check -s exit:0 -e empty -o match:4 \
		${TEST_SH} -c '< cat wc'


	cat <<- 'EOF' |
		for l in 1 2 3; do
			read line < File
			echo "$line"
		done
	EOF
	atf_check -s exit:0 -e empty \
		-o inline:'First Line\nFirst Line\nFirst Line\n' \
		${TEST_SH}

	cat <<- 'EOF' |
		for l in 1 2 3; do
			read line
			echo "$line"
		done <File
	EOF
	atf_check -s exit:0 -e empty \
		-o inline:'First Line\nSecond Line\nLine 3\n' \
		${TEST_SH}

	cat <<- 'EOF' |
		for l in 1 2 3; do
			read line < File
			echo "$line"
		done <File
	EOF
	atf_check -s exit:0 -e empty \
		-o inline:'First Line\nFirst Line\nFirst Line\n' \
		${TEST_SH}

	cat <<- 'EOF' |
		line=
		while [ "$line" != END ]; do
			read line || exit 1
			echo "$line"
		done <File
	EOF
	atf_check -s exit:0 -e empty \
		-o inline:'First Line\nSecond Line\nLine 3\nEND\n' \
		${TEST_SH}

	cat <<- 'EOF' |
		while :; do
			read line || exit 0
			echo "$line"
		done <File
	EOF
	atf_check -s exit:0 -e empty \
		-o inline:'First Line\nSecond Line\nLine 3\nEND\n' \
		${TEST_SH}

	cat <<- 'EOF' |
		l=''
		while read line < File
		do
			echo "$line"
			l="${l}x"
			[ ${#l} -ge 3 ] && break
		done
		echo DONE
	EOF
	atf_check -s exit:0 -e empty \
		-o inline:'First Line\nFirst Line\nFirst Line\nDONE\n' \
		${TEST_SH}

	cat <<- 'EOF' |
		while read line
		do
			echo "$line"
		done <File
		echo DONE
	EOF
	atf_check -s exit:0 -e empty \
		-o inline:'First Line\nSecond Line\nLine 3\nEND\nDONE\n' \
		${TEST_SH}

	cat <<- 'EOF' |
		l=''
		while read line
		do
			echo "$line"
			l="${l}x"
			[ ${#l} -ge 3 ] && break
		done <File
		echo DONE
	EOF
	atf_check -s exit:0 -e empty \
		-o inline:'First Line\nSecond Line\nLine 3\nDONE\n' ${TEST_SH}

	cat <<- 'EOF' |
		l=''
		while read line1 <File
		do
			read line2
			echo "$line1":"$line2"
			l="${l}x"
			[ ${#l} -ge 2 ] && break
		done <File
		echo DONE
	EOF
	atf_check -s exit:0 -e empty \
	    -o inline:'First Line:First Line\nFirst Line:Second Line\nDONE\n' \
		${TEST_SH}
}

atf_test_case do_output_redirections
do_output_redirections_head()
{
	atf_set "descr" "Test Output redirections"
}
do_output_redirections_body()
{
nl='
'
	T=0
	i() { T=$(expr "$T" + 1); }

	rm -f Output 2>/dev/null || :
	test -f Output && atf_fail "Unable to remove Output file"
#1
	i; atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c '> Output'
	test -f Output || atf_fail "#$T: Did not make Output file"
#2
	rm -f Output 2>/dev/null || :
	i; atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c '>> Output'
	test -f Output || atf_fail "#$T: Did not make Output file"
#3
	rm -f Output 2>/dev/null || :
	i; atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c '>| Output'
	test -f Output || atf_fail "#$T: Did not make Output file"

#4
	rm -f Output 2>/dev/null || :
	i
	atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c 'echo Hello >Output'
	test -s Output || atf_fail "#$T: Did not make non-empty Output file"
	test "$(cat Output)" = "Hello" ||
	  atf_fail "#$T: Incorrect Output: Should be 'Hello' is '$(cat Output)'"
#5
	i
	atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c 'echo Hello>!Output'
	test -s Output || atf_fail "#$T: Did not make non-empty Output file"
	test "$(cat Output)" = "Hello" ||
	  atf_fail "#$T: Incorrect Output: Should be 'Hello' is '$(cat Output)'"
#6
	i
	atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c 'echo Bye >>Output'
	test -s Output || atf_fail "#$T: Removed Output file"
	test "$(cat Output)" = "Hello${nl}Bye" || atf_fail \
	  "#$T: Incorrect Output: Should be 'Hello\\nBye' is '$(cat Output)'"
#7
	i; atf_check -s exit:0 -o inline:'line 1\nline 2\n' -e empty \
		${TEST_SH} -c \
		'echo line 1 > Output; echo line 2 >> Output; cat Output'
	test "$(cat Output)" = "line 1${nl}line 2" || atf_fail \
	 "#$T: Incorrect Output: Should be 'line 1\\nline 2' is '$(cat Output)'"
#8
	i; atf_check -s exit:0 -o inline:'line 2\n' -e empty \
		${TEST_SH} -c 'echo line 1 > Output; echo line 2'
	test "$(cat Output)" = "line 1" || atf_fail \
	    "#$T: Incorrect Output: Should be 'line 1' is '$(cat Output)'"
#9
	i; atf_check -s exit:0 -o empty -e empty \
		${TEST_SH} -c '(echo line 1; echo line 2 > Out2) > Out1'
	test "$(cat Out1)" = "line 1" || atf_fail \
	    "#$T: Incorrect Out1: Should be 'line 1' is '$(cat Out1)'"
	test "$(cat Out2)" = "line 2" || atf_fail \
	    "#$T: Incorrect Out2: Should be 'line 2' is '$(cat Out2)'"
#10
	i; atf_check -s exit:0 -o empty -e empty \
		${TEST_SH} -c '{ echo line 1; echo line 2 > Out2;} > Out1'
	test "$(cat Out1)" = "line 1" || atf_fail \
	    "#$T: Incorrect Out1: Should be 'line 1' is '$(cat Out1)'"
	test "$(cat Out2)" = "line 2" || atf_fail \
	    "#$T: Incorrect Out2: Should be 'line 2' is '$(cat Out2)'"
#11
	i; rm -f Out1 Out2 2>/dev/null || :
	cat <<- 'EOF' |
		for arg in 'line 1' 'line 2' 'line 3'
		do
			echo "$arg"
			echo "$arg" > Out1
		done > Out2
	EOF
	atf_check -s exit:0 -o empty -e empty ${TEST_SH} 
	test "$(cat Out1)" = "line 3" || atf_fail \
		"#$T:  Incorrect Out1: Should be 'line 3' is '$(cat Out1)'"
	test "$(cat Out2)" = "line 1${nl}line 2${nl}line 3" || atf_fail \
    "#$T: Incorrect Out2: Should be 'line 1\\nline 2\\nline 3' is '$(cat Out2)'"
#12
	i; rm -f Out1 Out2 2>/dev/null || :
	cat <<- 'EOF' |
		for arg in 'line 1' 'line 2' 'line 3'
		do
			echo "$arg"
			echo "$arg" >> Out1
		done > Out2
	EOF
	atf_check -s exit:0 -o empty -e empty ${TEST_SH} 
	test "$(cat Out1)" = "line 1${nl}line 2${nl}line 3" || atf_fail \
    "#$T: Incorrect Out1: Should be 'line 1\\nline 2\\nline 3' is '$(cat Out1)'"
	test "$(cat Out2)" = "line 1${nl}line 2${nl}line 3" || atf_fail \
    "#$T: Incorrect Out2: Should be 'line 1\\nline 2\\nline 3' is '$(cat Out2)'"
}

atf_test_case fd_redirections
fd_redirections_head()
{
	atf_set "descr" "Tests redirections to/from specific descriptors"
}
fd_redirections_body()
{
	atf_require_prog /bin/echo

	cat <<- 'DONE' > helper.sh
		f() {
			/bin/echo nothing "$1" >& "$1"
		}
		for n
		do
			eval "f $n $n"'> file-$n'
		done
	DONE
	cat <<- 'DONE' > reread.sh
		f() {
			(read -r var; echo "${var}") <&"$1"
		}
		for n
		do
			x=$( eval "f $n $n"'< file-$n' )
			test "${x}" = "nothing $n" || echo "$n"
		done
	DONE

	validate()
	{
	    for n
	    do
		test -e "file-$n" || atf_fail "file-$n not created"
		C=$(cat file-"$n")
		test "$C" = "nothing $n" ||
			atf_fail "file-$n contains '$C' not 'nothing $n'"
	    done
	}

	atf_check -s exit:0 -e empty -o empty \
		${TEST_SH} helper.sh 1 2 3 4 5 6 7 8 9
	validate 1 2 3 4 5 6 7 8 9
	atf_check -s exit:0 -e empty -o empty \
		${TEST_SH} reread.sh 3 4 5 6 7 8 9

	L=$(ulimit -n)
	if [ "$L" -ge 30 ]
	then
		atf_check -s exit:0 -e empty -o empty \
			${TEST_SH} helper.sh 10 15 19 20 25 29
		validate 10 15 19 20 25 29
		atf_check -s exit:0 -e empty -o empty \
			${TEST_SH} reread.sh 10 15 19 20 25 29
	fi
	if [ "$L" -ge 100 ]
	then
		atf_check -s exit:0 -e empty -o empty \
			${TEST_SH} helper.sh 32 33 49 50 51 63 64 65 77 88 99
		validate 32 33 49 50 51 63 64 65 77 88 99
		atf_check -s exit:0 -e empty -o empty \
			${TEST_SH} reread.sh 32 33 49 50 51 63 64 65 77 88 99
	fi
	if [ "$L" -ge 500 ]
	then
		atf_check -s exit:0 -e empty -o empty \
			${TEST_SH} helper.sh 100 101 199 200 222 333 444 499
		validate 100 101 199 200 222 333 444 499
		atf_check -s exit:0 -e empty -o empty \
			${TEST_SH} reread.sh 100 101 199 200 222 333 444 499
	fi
	if [ "$L" -gt 1005 ]
	then
		atf_check -s exit:0 -e empty -o empty \
			${TEST_SH} helper.sh 1000 1001 1002 1003 1004 1005
		validate 1000 1001 1002 1003 1004 1005
		atf_check -s exit:0 -e empty -o empty \
			${TEST_SH} reread.sh 1000 1001 1002 1003 1004 1005
	fi
} 

atf_test_case local_redirections
local_redirections_head()
{
	atf_set "descr" \
	    "Tests that exec can reassign file descriptors in the shell itself"
}
local_redirections_body()
{
	cat <<- 'DONE' > helper.sh
		for f
		do
			eval "exec $f"'> file-$f'
		done

		for f
		do
			printf '%s\n' "Hello $f" >&"$f"
		done

		for f
		do
			eval "exec $f"'>&-'
		done

		for f
		do
			eval "exec $f"'< file-$f'
		done

		for f
		do
			exec <& "$f"
			read -r var || echo >&2 "No data in file-$f"
			read -r x && echo >&2 "Too much data in file-${f}: $x"
			test "${var}" = "Hello $f" ||
			    echo >&2 "file-$f contains '${var}' not 'Hello $f'"
		done
	DONE

	atf_check -s exit:0 -o empty -e empty \
		${TEST_SH} helper.sh 3 4 5 6 7 8 9

	L=$(ulimit -n)
	if [ "$L" -ge 30 ]
	then
		atf_check -s exit:0 -o empty -e empty \
			${TEST_SH} helper.sh 10 11 13 15 16 19 20 28 29
	fi
	if [ "$L" -ge 100 ]
	then
		atf_check -s exit:0 -o empty -e empty \
			${TEST_SH} helper.sh 30 31 32 63 64 65 77 88 99
	fi
	if [ "$L" -ge 500 ]
	then
		atf_check -s exit:0 -o empty -e empty \
			${TEST_SH} helper.sh 100 101 111 199 200 201 222 333 499
	fi
	if [ "$L" -ge 1005 ]
	then
		atf_check -s exit:0 -o empty -e empty \
			${TEST_SH} helper.sh 1000 1001 1002 1003 1004 1005
	fi
}

atf_test_case named_fd_redirections
named_fd_redirections_head()
{
	atf_set "descr" "Tests redirections to /dev/stdout (etc)"

}
named_fd_redirections_body()
{
	if test -c /dev/stdout
	then
		atf_check -s exit:0 -o inline:'OK\n' -e empty \
			${TEST_SH} -c 'echo OK >/dev/stdout'
		atf_check -s exit:0 -o inline:'OK\n' -e empty \
			${TEST_SH} -c '/bin/echo OK >/dev/stdout'
	fi

	if test -c /dev/stdin
	then
		atf_require_prog cat

		echo GOOD | atf_check -s exit:0 -o inline:'GOOD\n' -e empty \
			${TEST_SH} -c 'read var </dev/stdin; echo $var'
		echo GOOD | atf_check -s exit:0 -o inline:'GOOD\n' -e empty \
			${TEST_SH} -c 'cat </dev/stdin'
	fi

	if test -c /dev/stderr
	then
		atf_check -s exit:0 -e inline:'OK\n' -o empty \
			${TEST_SH} -c 'echo OK 2>/dev/stderr >&2'
		atf_check -s exit:0 -e inline:'OK\n' -o empty \
			${TEST_SH} -c '/bin/echo OK 2>/dev/stderr >&2'
	fi

	if test -c /dev/fd/8 && test -c /dev/fd/9
	then
		atf_check -s exit:0 -o inline:'EIGHT\n' -e empty \
			${TEST_SH} -c 'printf "%s\n" EIGHT 8>&1 >/dev/fd/8 |
					cat 9<&0 </dev/fd/9'
	fi

	return 0
}

atf_test_case redir_in_case
redir_in_case_head()
{
	atf_set "descr" "Tests that sh(1) allows just redirections " \
	                "in case statements. (PR bin/48631)"
}
redir_in_case_body()
{
	atf_check -s exit:0 -o empty -e empty \
	    ${TEST_SH} -c 'case x in (whatever) >foo;; esac'

	atf_check -s exit:0 -o empty -e empty \
	    ${TEST_SH} -c 'case x in (whatever) >foo 2>&1;; esac'

	atf_check -s exit:0 -o empty -e empty \
	    ${TEST_SH} -c 'case x in (whatever) >foo 2>&1 </dev/null;; esac'

	atf_check -s exit:0 -o empty -e empty \
	    ${TEST_SH} -c 'case x in (whatever) >${somewhere};; esac'
}

atf_test_case incorrect_redirections
incorrect_redirections_head()
{
	atf_set "descr" "Tests that sh(1) correctly ignores non-redirections"
}
incorrect_redirections_body() {

	atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} -c 'echo foo>'
	atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} -c 'read foo<'
	atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} -c 'echo foo<>'
	atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} -c \
		'echo x > '"$nl"
	atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} -c \
		'read x < '"$nl"
	atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} -c \
		'echo x <> '"$nl"
	atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} -c \
		'echo x >< anything'
	atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} -c \
		'echo x >>< anything'
	atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} -c \
		'echo x >|< anything'
	atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} -c \
		'echo x > ; read x < /dev/null || echo bad'
	atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} -c \
		'read x < & echo y > /dev/null; wait && echo bad'

	rm -f Output 2>/dev/null || :
	atf_check -s exit:0 -e empty -o inline:'A Line > Output\n' \
		${TEST_SH} -c 'echo A Line \> Output'
	test -f Output && atf_file "File 'Output' appeared and should not have"

	rm -f Output 2>/dev/null || :
	atf_check -s exit:0 -e empty -o empty \
		${TEST_SH} -c 'echo A Line \>> Output'
	test -f Output || atf_file "File 'Output' not created when it should"
	test "$(cat Output)" = 'A Line >' || atf_fail \
		"Output file contains '$(cat Output)' instead of '"'A Line >'\'

	rm -f Output \> 2>/dev/null || :
	atf_check -s exit:0 -e empty -o empty \
		${TEST_SH} -c 'echo A Line >\> Output'
	test -f Output && atf_file "File 'Output' appeared and should not have"
	test -f '>' || atf_file "File '>' not created when it should"
	test "$(cat '>')" = 'A Line Output' || atf_fail \
	    "Output file ('>') contains '$(cat '>')' instead of 'A Line Output'"
}

# Many more tests in t_here, so here we have just rudimentary checks
atf_test_case redir_here_doc
redir_here_doc_head()
{
	atf_set "descr" "Tests that sh(1) correctly processes 'here' doc " \
	                "input redirections"
}
redir_here_doc_body()
{
	# nb: the printf is not executed, it is data
	cat <<- 'DONE' |
		cat <<EOF
			printf '%s\n' 'hello\n'
		EOF
	DONE
	atf_check -s exit:0 -o match:printf -o match:'hello\\n' \
		-e empty ${TEST_SH} 
}

atf_test_case subshell_redirections
subshell_redirections_head()
{
	atf_set "descr" "Tests redirection interactions between shell and " \
			"its sub-shell(s)"
}
subshell_redirections_body()
{
	atf_require_prog cat

	LIM=$(ulimit -n)

	cat <<- 'DONE' |
		exec 6>output-file

		( printf "hello\n" >&6 )

		exec 8<output-file

		( read hello <&8 ; test hello = "$hello" || echo >&2 Hello )

		( printf "bye-bye\n" >&6 )

		( exec 8<&- )
		read bye <&8 || echo >&2 "Closed?"
		echo Bye="$bye"
	DONE
	atf_check -s exit:0 -o match:Bye=bye-bye -e empty \
		${TEST_SH}

	cat <<- 'DONE' |
		for arg in one-4 two-24 three-14
		do
			fd=${arg#*-}
			file=${arg%-*}
			eval "exec ${fd}>${file}"
		done

		for arg in one-5 two-7 three-19
		do
			fd=${arg#*-}
			file=${arg%-*}
			eval "exec ${fd}<${file}"
		done

		(
			echo line-1 >&4
			echo line-2 >&24
			echo line-3 >&14
			echo go
		) | (
			read go
			read x <&5
			read y <&7
			read z <&19

			printf "%s\n" "${x}" "${y}" "${z}"
		)
	DONE
	atf_check -s exit:0 -o inline:'line-1\nline-2\nline-3\n' \
		-e empty ${TEST_SH}

	cat <<- 'DONE' |
		for arg in one-4-5 two-6-7 three-8-9 four-11-10 five-3-12
		do
			ofd=${arg##*-}
			file=${arg%-*}
			ifd=${file#*-}
			file=${file%-*}
			eval "exec ${ofd}>${file}"
			eval "exec ${ifd}<${file}"
		done

		( ( ( echo line-1 >& 13 ) 13>&12 ) 12>&5 ) >stdout 2>errout
		( ( ( echo line-2 >& 4) 13>&12 ) 4>&7 ) >>stdout 2>>errout
		( ( ( echo line-3 >& 6) 8>&1 6>&11 >&12) 11>&9 >&7 ) >>stdout

		( ( ( cat <&13 >&12 ) 13<&8 12>&10 ) 10>&1 8<&6 ) 6<&4
		( ( ( cat <&4 ) <&4 6<&8 8<&11  )
			<&4 4<&6 6<&8 8<&11 ) <&4 4<&6 6<&8 8<&11 11<&3
		( ( ( cat <&7 >&1 ) 7<&6 >&10 ) 10>&2 6<&8 ) 2>&1
	DONE
	atf_check -s exit:0 -o inline:'line-1\nline-2\nline-3\n' \
		-e empty ${TEST_SH}
}

atf_test_case ulimit_redirection_interaction
ulimit_redirection_interaction_head()
{
	atf_set "descr" "Tests interactions between redirect and ulimit -n "
}
ulimit_redirection_interaction_body()
{
	atf_require_prog ls

	cat <<- 'DONE' > helper.sh
		oLIM=$(ulimit -n)
		HRD=$(ulimit -H -n)
		test "${oLIM}" -lt "${HRD}"  && ulimit -n "${HRD}"
		LIM=$(ulimit -n)

		FDs=
		LFD=-1
		while [ ${LIM} -gt 16 ]
		do
			FD=$(( ${LIM} - 1 ))
			if [ "${FD}" -eq "${LFD}" ]; then
				echo >&2 "Infinite loop... (busted $(( )) ??)"
				exit 1
			fi
			LFD="${FD}"

			eval "exec ${FD}"'> /dev/null'
			FDs="${FD}${FDs:+ }${FDs}"

			(
				FD=$(( ${LIM} + 1 ))
				eval "exec ${FD}"'> /dev/null'
				echo "Reached unreachable command"
			) 2>/dev/null && echo >&2 "Opened beyond limit!"

			(eval 'ls 2>&1 3>&1 4>&1 5>&1 '"${FD}"'>&1') >&"${FD}"

			LIM=$(( ${LIM} / 2 ))
			ulimit -S -n "${LIM}"
		done

		# Even though ulimit has been reduced, open fds should work
		for FD in ${FDs}
		do
			echo ${FD} in ${FDs} >&"${FD}" || exit 1
		done

		ulimit -S -n "${oLIM}"

		# maybe more later...

	DONE

	atf_check -s exit:0 -o empty -e empty ${TEST_SH} helper.sh
}

atf_test_case validate_fn_redirects
validate_fn_redirects_head()
{
	# These test cases inspired by PR bin/48875 and the sh
	# changes that were required to fix it.

	atf_set "descr" "Tests various redirections applied to functions " \
		"See PR bin/48875"
}
validate_fn_redirects_body()
{
	cat <<- 'DONE' > f-def
		f() {
			printf '%s\n' In-Func
		}
	DONE

	atf_check -s exit:0 -o inline:'In-Func\nsuccess1\n' -e empty \
		${TEST_SH} -c ". ./f-def; f ; printf '%s\n' success1"
	atf_check -s exit:0 -o inline:'success2\n' -e empty \
		${TEST_SH} -c ". ./f-def; f >/dev/null; printf '%s\n' success2"
	atf_check -s exit:0 -o inline:'success3\n' -e empty \
		${TEST_SH} -c ". ./f-def; f >&- ; printf '%s\n' success3"
	atf_check -s exit:0 -o inline:'In-Func\nsuccess4\n' -e empty \
		${TEST_SH} -c ". ./f-def; f & wait; printf '%s\n' success4"
	atf_check -s exit:0 -o inline:'success5\n' -e empty \
		${TEST_SH} -c ". ./f-def; f >&- & wait; printf '%s\n' success5"
	atf_check -s exit:0 -o inline:'In-Func\nIn-Func\nsuccess6\n' -e empty \
		${TEST_SH} -c ". ./f-def; f;f; printf '%s\n' success6"
	atf_check -s exit:0 -o inline:'In-Func\nIn-Func\nsuccess7\n' -e empty \
		${TEST_SH} -c ". ./f-def; { f;f;}; printf '%s\n' success7"
	atf_check -s exit:0 -o inline:'In-Func\nIn-Func\nsuccess8\n' -e empty \
		${TEST_SH} -c ". ./f-def; { f;f;}& wait; printf '%s\n' success8"
	atf_check -s exit:0 -o inline:'In-Func\nsuccess9\n' -e empty \
		${TEST_SH} -c \
		   ". ./f-def; { f>/dev/null;f;}& wait; printf '%s\n' success9"
	atf_check -s exit:0 -o inline:'In-Func\nsuccess10\n' -e empty \
		${TEST_SH} -c \
		   ". ./f-def; { f;f>/dev/null;}& wait; printf '%s\n' success10"

	# This one tests the issue etcupdate had with the original 48875 fix
	atf_check -s exit:0 -o inline:'Func a\nFunc b\nFunc c\n' -e empty \
		${TEST_SH} -c '
			f() {
				echo Func "$1"
			}
			exec 3<&0 4>&1
			( echo x-a; echo y-b; echo z-c ) |
			while read A
			do
				B=${A#?-}
				f "$B" <&3 >&4
			done >&2'

	# And this tests a similar condition with that same fix
	cat  <<- 'DONE' >Script
		f() {
			printf '%s' " hello $1"
		}
		exec 3>&1
		echo $( for i in a b c
			do printf '%s' @$i; f $i >&3; done >foo
		)
		printf '%s\n' foo=$(cat foo)
	DONE
	atf_check -s exit:0 -e empty \
	    -o inline:' hello a hello b hello c\nfoo=@a@b@c\n' \
	    ${TEST_SH} Script

	# Tests with sh reading stdin, which is not quite the same internal
	# mechanism.
	echo ". ./f-def || echo >&2 FAIL
		f
		printf '%s\n' stdin1
	"| atf_check -s exit:0 -o inline:'In-Func\nstdin1\n' -e empty ${TEST_SH}

	echo '
		. ./f-def || echo >&2 FAIL
		f >&-
		printf "%s\n" stdin2
	' | atf_check -s exit:0 -o inline:'stdin2\n' -e empty ${TEST_SH}

	cat <<- 'DONE' > fgh.def
		f() {
			echo -n f >&3
			sleep 4
			echo -n F >&3
		}
		g() {
			echo -n g >&3
			sleep 2
			echo -n G >&3
		}
		h() {
			echo -n h >&3
		}
	DONE

	atf_check -s exit:0 -o inline:'fFgGh' -e empty \
		${TEST_SH} -c '. ./fgh.def || echo >&2 FAIL
			exec 3>&1
			f; g; h'

	atf_check -s exit:0 -o inline:'fghGF' -e empty \
		${TEST_SH} -c '. ./fgh.def || echo >&2 FAIL
			exec 3>&1
			f & sleep 1; g & sleep 1; h; wait'

	atf_check -s exit:0 -o inline:'fFgGhX Y\n' -e empty \
		${TEST_SH} -c '. ./fgh.def || echo >&2 FAIL
			exec 3>&1
			echo X $( f ; g ; h ) Y'

	# This one is the real test for PR bin/48875.  If the
	# cmdsub does not complete before f g (and h) exit,
	# then the 'F' & 'G' will precede 'X Y' in the output.
	# If the cmdsub finishes while f & g are still running,
	# then the X Y will appear before the F and G.
	# The trailing "sleep 3" is just so we catch all the
	# output (otherwise atf_check will be finished while
	# f & g are still sleeping).

	atf_check -s exit:0 -o inline:'fghX Y\nGF' -e empty \
		${TEST_SH} -c '. ./fgh.def || echo >&2 FAIL
			exec 3>&1
			echo X $( f >&- & sleep 1; g >&- & sleep 1 ; h ) Y
			sleep 3
			exec 4>&1 || echo FD_FAIL
			'

	# Do the test again to verify it also all works reading stdin
	# (which is a slightly different path through the shell)
	echo '
		. ./fgh.def || echo >&2 FAIL
		exec 3>&1
		echo X $( f >&- & sleep 1; g >&- & sleep 1 ; h ) Y
		sleep 3
		exec 4>&1 || echo FD_FAIL
	' | atf_check -s exit:0 -o inline:'fghX Y\nGF' -e empty ${TEST_SH}
}

atf_init_test_cases() {
	atf_add_test_case basic_test_method_test
	atf_add_test_case do_input_redirections
	atf_add_test_case do_output_redirections
	atf_add_test_case fd_redirections
	atf_add_test_case local_redirections
	atf_add_test_case incorrect_redirections
	atf_add_test_case named_fd_redirections
	atf_add_test_case redir_here_doc
	atf_add_test_case redir_in_case
	atf_add_test_case subshell_redirections
	atf_add_test_case ulimit_redirection_interaction
	atf_add_test_case validate_fn_redirects
}
