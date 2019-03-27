# $NetBSD: t_option.sh,v 1.3 2016/03/08 14:19:28 christos Exp $
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

# The standard
# http://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html
# says:
#	...[lots]

test_option_on_off()
{
	atf_require_prog tr

	for opt
	do
				# t is needed, as inside $()` $- appears to lose
				# the 'e' option if it happened to already be
				# set.  Must check if that is what should
				# happen, but that is a different issue.

		test -z "${opt}" && continue

		# if we are playing with more that one option at a
		# time, the code below requires that we start with no
		# options set, or it will mis-diagnose the situation
		CLEAR=''
		test "${#opt}" -gt 1 &&
  CLEAR='xx="$-" && xx=$(echo "$xx" | tr -d cs) && test -n "$xx" && set +"$xx";'

		atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c \
			"opt=${opt}"'
			x() {
				echo "ERROR: Unable to $1 option $2" >&2
				exit 1
			}
			s() {
				set -"$1"
				t="$-"
				x=$(echo "$t" | tr -d "$1")
				test "$t" = "$x" && x set "$1"
				return 0
			}
			c() {
				set +"$1"
				t="$-"
				x=$(echo "$t" | tr -d "$1")
				test "$t" != "$x" && x clear "$1"
				return 0
			}
			'"${CLEAR}"'

			# if we do not do this, -x tracing splatters stderr
			# for some shells, -v does as well (is that correct?)
			case "${opt}" in
			(*[xv]*)	exec 2>/dev/null;;
			esac

			o="$-"
			x=$(echo "$o" | tr -d "$opt")

			if [ "$o" = "$x" ]; then	# option was off
				s "${opt}"
				c "${opt}"
			else
				c "${opt}"
				s "${opt}"
			fi
		'
	done
}

test_optional_on_off()
{
	RET=0
	OPTS=
	for opt
	do
		test "${opt}" = n && continue
		${TEST_SH} -c "set -${opt}" 2>/dev/null  &&
			OPTS="${OPTS} ${opt}" || RET=1
	done

	test -n "${OPTS}" && test_option_on_off ${OPTS}

	return "${RET}"
}

atf_test_case set_a
set_a_head() {
	atf_set "descr" "Tests that 'set -a' turns on all var export " \
	                "and that it behaves as defined by the standard"
}
set_a_body() {
	atf_require_prog env
	atf_require_prog grep

	test_option_on_off a

	# without -a, new variables should not be exported (so grep "fails")
	atf_check -s exit:1 -o empty -e empty ${TEST_SH} -ce \
		'unset VAR; set +a; VAR=value; env | grep "^VAR="'

	# with -a, they should be
	atf_check -s exit:0 -o match:VAR=value -e empty ${TEST_SH} -ce \
		'unset VAR; set -a; VAR=value; env | grep "^VAR="'
}

atf_test_case set_C
set_C_head() {
	atf_set "descr" "Tests that 'set -C' turns on no clobber mode " \
	                "and that it behaves as defined by the standard"
}
set_C_body() {
	atf_require_prog ls

	test_option_on_off C

	# Check that the environment to use for the tests is sane ...
	# we assume current dir is a new tempory directory & is empty

	test -z "$(ls)" || atf_skip "Test execution directory not clean"
	test -c "/dev/null" || atf_skip "Problem with /dev/null"

	echo Dummy_Content > Junk_File
	echo Precious_Content > Important_File

	# Check that we can redirect onto file when -C is not set
	atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c \
		'
		D=$(ls -l Junk_File) || exit 1
		set +C
		echo "Overwrite it now" > Junk_File
		A=$(ls -l Junk_File) || exit 1
		test "${A}" != "${D}"
		'

	# Check that we cannot redirect onto file when -C is set
	atf_check -s exit:0 -o empty -e not-empty ${TEST_SH} -c \
		'
		D=$(ls -l Important_File) || exit 1
		set -C
		echo "Fail to Overwrite it now" > Important_File
		A=$(ls -l Important_File) || exit 1
		test "${A}" = "${D}"
		'

	# Check that we can append to file, even when -C is set
	atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c \
		'
		D=$(ls -l Junk_File) || exit 1
		set -C
		echo "Append to it now" >> Junk_File
		A=$(ls -l Junk_File) || exit 1
		test "${A}" != "${D}"
		'

	# Check that we abort on attempt to redirect onto file when -Ce is set
	atf_check -s not-exit:0 -o empty -e not-empty ${TEST_SH} -c \
		'
		set -Ce
		echo "Fail to Overwrite it now" > Important_File
		echo "Should not reach this point"
		'

	# Last check that we can override -C for when we really need to
	atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c \
		'
		D=$(ls -l Junk_File) || exit 1
		set -C
		echo "Change the poor bugger again" >| Junk_File
		A=$(ls -l Junk_File) || exit 1
		test "${A}" != "${D}"
		'
}

atf_test_case set_e
set_e_head() {
	atf_set "descr" "Tests that 'set -e' turns on error detection " \
		"and that a simple case behaves as defined by the standard"
}
set_e_body() {
	test_option_on_off e

	# Check that -e does nothing if no commands fail
	atf_check -s exit:0 -o match:I_am_OK -e empty \
	    ${TEST_SH} -c \
		'false; printf "%s" I_am; set -e; true; printf "%s\n" _OK'

	# and that it (silently, but with exit status) aborts if cmd fails
	atf_check -s not-exit:0 -o match:I_am -o not-match:Broken -e empty \
	    ${TEST_SH} -c \
		'false; printf "%s" I_am; set -e; false; printf "%s\n" _Broken'

	# same, except -e this time is on from the beginning
	atf_check -s not-exit:0 -o match:I_am -o not-match:Broken -e empty \
	    ${TEST_SH} -ec 'printf "%s" I_am; false; printf "%s\n" _Broken'

	# More checking of -e in other places, there is lots to deal with.
}

atf_test_case set_f
set_f_head() {
	atf_set "descr" "Tests that 'set -f' turns off pathname expansion " \
	                "and that it behaves as defined by the standard"
}
set_f_body() {
	atf_require_prog ls

	test_option_on_off f

	# Check that the environment to use for the tests is sane ...
	# we assume current dir is a new tempory directory & is empty

	test -z "$(ls)" || atf_skip "Test execution directory not clean"

	# we will assume that atf will clean up this junk directory
	# when we are done.   But for testing pathname expansion
	# we need files

	for f in a b c d e f aa ab ac ad ae aaa aab aac aad aba abc bbb ccc
	do
		echo "$f" > "$f"
	done

	atf_check -s exit:0 -o empty -e empty ${TEST_SH} -ec \
	    'X=$(echo b*); Y=$(echo b*); test "${X}" != "a*";
		test "${X}" = "${Y}"'

	# now test expansion is different when -f is set
	atf_check -s exit:0 -o empty -e empty ${TEST_SH} -ec \
	   'X=$(echo b*); Y=$(set -f; echo b*); test "${X}" != "${Y}"'
}

atf_test_case set_n
set_n_head() {
	atf_set "descr" "Tests that 'set -n' supresses command execution " \
	                "and that it behaves as defined by the standard"
}
set_n_body() {
	# pointless to test this, if it turns on, it stays on...
	# test_option_on_off n
	# so just allow the tests below to verify it can be turned on

	# nothing should be executed, hence no output...
	atf_check -s exit:0 -o empty -e empty \
		${TEST_SH} -enc 'echo ABANDON HOPE; echo ALL YE; echo ...'

	# this is true even when the "commands" do not exist
	atf_check -s exit:0 -o empty -e empty \
		${TEST_SH} -enc 'ERR; FAIL; ABANDON HOPE'

	# but if there is a syntax error, it should be detected (w or w/o -e)
	atf_check -s not-exit:0 -o empty -e not-empty \
		${TEST_SH} -enc 'echo JUMP; for frogs swim; echo in puddles'
	atf_check -s not-exit:0 -o empty -e not-empty \
		${TEST_SH} -nc 'echo ABANDON HOPE; echo "ALL YE; echo ...'
	atf_check -s not-exit:0 -o empty -e not-empty \
		${TEST_SH} -enc 'echo ABANDON HOPE;; echo ALL YE; echo ...'
	atf_check -s not-exit:0 -o empty -e not-empty \
		${TEST_SH} -nc 'do YOU ABANDON HOPE; for all eternity?'

	# now test enabling -n in the middle of a script
	# note that once turned on, it cannot be turned off again.
	#
	# omit more complex cases, as those can send some shells
	# into infinite loops, and believe it or not, that might be OK!

	atf_check -s exit:0 -o match:first -o not-match:second -e empty \
		${TEST_SH} -c 'echo first; set -n; echo second'
	atf_check -s exit:0 -o match:first -o not-match:third -e empty \
	    ${TEST_SH} -c 'echo first; set -n; echo second; set +n; echo third'
	atf_check -s exit:0 -o inline:'a\nb\n' -e empty \
	    ${TEST_SH} -c 'for x in a b c d
			   do
				case "$x" in
				     a);; b);; c) set -n;; d);;
				esac
				printf "%s\n" "$x"
			   done'

	# This last one is a bit more complex to explain, so I will not try

	# First, we need to know what signal number is used for SIGUSR1 on
	# the local (testing) system (signal number is $(( $XIT - 128 )) )

	# this will take slightly over 1 second elapsed time (the sleep 1)
	# The "10" for the first sleep just needs to be something big enough
	# that the rest of the commands have time to complete, even on
	# very slow testing systems.  10 should be enough.  Otherwise irrelevant

	# The shell will usually blather to stderr about the sleep 10 being
	# killed, but it affects nothing, so just allow it to cry.

	(sleep 10 & sleep 1; kill -USR1 $!; wait $!)
	XIT="$?"

	# The exit value should be an integer > 128 and < 256 (often 158)
	# If it is not just skip the test

	# If we do run the test, it should take (slightly over) either 1 or 2
	# seconds to complete, depending upon the shell being tested.

	case "${XIT}" in
	( 129 | 1[3-9][0-9] | 2[0-4][0-9] | 25[0-5] )

		# The script below should exit with the same code - no output

		# Or that is the result that seems best explanable.
		# "set -n" in uses like this is not exactly well defined...

		# This script comes from a member of the austin group
		# (they author changes to the posix shell spec - and more.)
		# The author is also an (occasional?) NetBSD user.
		atf_check -s exit:${XIT} -o empty -e empty ${TEST_SH} -c '
			trap "set -n" USR1
			{ sleep 1; kill -USR1 $$; sleep 1; } &
			false
			wait && echo t || echo f
			wait
			echo foo
		'
		;;
	esac
}

atf_test_case set_u
set_u_head() {
	atf_set "descr" "Tests that 'set -u' turns on unset var detection " \
	                "and that it behaves as defined by the standard"
}
set_u_body() {
	test_option_on_off u

	# first make sure it is OK to unset an unset variable
	atf_check -s exit:0 -o match:OK -e empty ${TEST_SH} -ce \
		'unset _UNSET_VARIABLE_; echo OK'
	# even if -u is set
	atf_check -s exit:0 -o match:OK -e empty ${TEST_SH} -cue \
		'unset _UNSET_VARIABLE_; echo OK'

	# and that without -u accessing an unset variable is harmless
	atf_check -s exit:0 -o match:OK -e empty ${TEST_SH} -ce \
		'unset X; echo ${X}; echo OK'
	# and that the unset variable test expansion works properly
	atf_check -s exit:0 -o match:OKOK -e empty ${TEST_SH} -ce \
		'unset X; printf "%s" ${X-OK}; echo OK'

	# Next test that with -u set, the shell aborts on access to unset var
	# do not use -e, want to make sure it is -u that causes abort
	atf_check -s not-exit:0 -o not-match:ERR -e not-empty ${TEST_SH} -c \
		'unset X; set -u; echo ${X}; echo ERR'
	# quoting should make no difference...
	atf_check -s not-exit:0 -o not-match:ERR -e not-empty ${TEST_SH} -c \
		'unset X; set -u; echo "${X}"; echo ERR'

	# Now a bunch of accesses to unset vars, with -u, in ways that are OK
	atf_check -s exit:0 -o match:OK -e empty ${TEST_SH} -ce \
		'unset X; set -u; echo ${X-GOOD}; echo OK'
	atf_check -s exit:0 -o match:OK -e empty ${TEST_SH} -ce \
		'unset X; set -u; echo ${X-OK}'
	atf_check -s exit:0 -o not-match:ERR -o match:OK -e empty \
		${TEST_SH} -ce 'unset X; set -u; echo ${X+ERR}; echo OK'

	# and some more ways that are not OK
	atf_check -s not-exit:0 -o not-match:ERR -e not-empty ${TEST_SH} -c \
		'unset X; set -u; echo ${X#foo}; echo ERR'
	atf_check -s not-exit:0 -o not-match:ERR -e not-empty ${TEST_SH} -c \
		'unset X; set -u; echo ${X%%bar}; echo ERR'

	# lastly, just while we are checking unset vars, test aborts w/o -u
	atf_check -s not-exit:0 -o not-match:ERR -e not-empty ${TEST_SH} -c \
		'unset X; echo ${X?}; echo ERR'
	atf_check -s not-exit:0 -o not-match:ERR -e match:X_NOT_SET \
		${TEST_SH} -c 'unset X; echo ${X?X_NOT_SET}; echo ERR'
}

atf_test_case set_v
set_v_head() {
	atf_set "descr" "Tests that 'set -v' turns on input read echoing " \
	                "and that it behaves as defined by the standard"
}
set_v_body() {
	test_option_on_off v

	# check that -v does nothing if no later input line is read
	atf_check -s exit:0 \
			-o match:OKOK -o not-match:echo -o not-match:printf \
			-e empty \
		${TEST_SH} -ec 'printf "%s" OK; set -v; echo OK; exit 0'

	# but that it does when there are multiple lines
	cat <<- 'EOF' |
		set -v
		printf %s OK
		echo OK
		exit 0
	EOF
	atf_check -s exit:0 \
			-o match:OKOK -o not-match:echo -o not-match:printf \
			-e match:printf -e match:OK -e match:echo \
			-e not-match:set ${TEST_SH}

	# and that it can be disabled again
	cat <<- 'EOF' |
		set -v
		printf %s OK
		set +v
		echo OK
		exit 0
	EOF
	atf_check -s exit:0 \
			-o match:OKOK -o not-match:echo -o not-match:printf \
			-e match:printf -e match:OK -e not-match:echo \
				${TEST_SH}

	# and lastly, that shell keywords do get output when "read"
	cat <<- 'EOF' |
		set -v
		for i in 111 222 333
		do
			printf %s $i
		done
		exit 0
	EOF
	atf_check -s exit:0 \
			-o match:111222333 -o not-match:printf \
			-o not-match:for -o not-match:do -o not-match:done \
			-e match:printf -e match:111 -e not-match:111222 \
			-e match:for -e match:do -e match:done \
				${TEST_SH}
}

atf_test_case set_x
set_x_head() {
	atf_set "descr" "Tests that 'set -x' turns on command exec logging " \
	                "and that it behaves as defined by the standard"
}
set_x_body() {
	test_option_on_off x

	# check that cmd output appears after -x is enabled
	atf_check -s exit:0 \
			-o match:OKOK -o not-match:echo -o not-match:printf \
			-e not-match:printf -e match:OK -e match:echo \
		${TEST_SH} -ec 'printf "%s" OK; set -x; echo OK; exit 0'

	# and that it stops again afer -x is disabled
	atf_check -s exit:0 \
			-o match:OKOK -o not-match:echo -o not-match:printf \
			-e match:printf -e match:OK -e not-match:echo \
	    ${TEST_SH} -ec 'set -x; printf "%s" OK; set +x; echo OK; exit 0'

	# also check that PS4 is output correctly
	atf_check -s exit:0 \
			-o match:OK -o not-match:echo \
			-e match:OK -e match:Run:echo \
		${TEST_SH} -ec 'PS4=Run:; set -x; echo OK; exit 0'

	return 0

	# This one seems controversial... I suspect it is NetBSD's sh
	# that is wrong to not output "for" "while" "if" ... etc

	# and lastly, that shell keywords do not get output when "executed"
	atf_check -s exit:0 \
			-o match:111222333 -o not-match:printf \
			-o not-match:for \
			-e match:printf -e match:111 -e not-match:111222 \
			-e not-match:for -e not-match:do -e not-match:done \
		${TEST_SH} -ec \
	   'set -x; for i in 111 222 333; do printf "%s" $i; done; echo; exit 0'
}

opt_test_setup()
{
	test -n "$1" || { echo >&2 "Internal error"; exit 1; }

	cat > "$1" << 'END_OF_FUNCTIONS'
local_opt_check()
{
	local -
}

instr()
{
	expr "$2" : "\(.*$1\)" >/dev/null
}

save_opts()
{
	local -

	set -e
	set -u

	instr e "$-" && instr u "$-" && return 0
	echo ERR
}

fiddle_opts()
{
	set -e
	set -u

	instr e "$-" && instr u "$-" && return 0
	echo ERR
}

local_test()
{
	set +eu

	save_opts
	instr '[eu]' "$-" || printf %s "OK"

	fiddle_opts
	instr e "$-" && instr u "$-" && printf %s "OK"

	set +eu
}
END_OF_FUNCTIONS
}

atf_test_case restore_local_opts
restore_local_opts_head() {
	atf_set "descr" "Tests that 'local -' saves and restores options.  " \
			"Note that "local" is a local shell addition"
}
restore_local_opts_body() {
	atf_require_prog cat
	atf_require_prog expr

	FN="test-funcs.$$"
	opt_test_setup "${FN}" || atf_skip "Cannot setup test environment"

	${TEST_SH} -ec ". './${FN}'; local_opt_check" 2>/dev/null ||
		atf_skip "sh extension 'local -' not supported by ${TEST_SH}"

	atf_check -s exit:0 -o match:OKOK -o not-match:ERR -e empty \
		${TEST_SH} -ec ". './${FN}'; local_test"
}

atf_test_case vi_emacs_VE_toggle
vi_emacs_VE_toggle_head() {
	atf_set "descr" "Tests enabling vi disables emacs (and v.v - but why?)"\
			"  Note that -V and -E are local shell additions"
}
vi_emacs_VE_toggle_body() {

	test_optional_on_off V E ||
	  atf_skip "One or both V & E opts unsupported by ${TEST_SH}"

	atf_check -s exit:0 -o empty -e empty ${TEST_SH} -c '
		q() {
			eval "case \"$-\" in
			(*${2}*)	return 1;;
			(*${1}*)	return 0;;
			esac"
			return 1
		}
		x() {
			echo >&2 "Option set or toggle failure:" \
					" on=$1 off=$2 set=$-"
			exit 1
		}
		set -V; q V E || x V E
		set -E; q E V || x E V
		set -V; q V E || x V E
		set +EV; q "" "[VE]" || x "" VE
		exit 0
	'
}

atf_test_case xx_bogus
xx_bogus_head() {
	atf_set "descr" "Tests that attempting to set a nonsense option fails."
}
xx_bogus_body() {
	# Biggest problem here is picking a "nonsense option" that is
	# not implemented by any shell, anywhere.  Hopefully this will do.

	# 'set' is a special builtin, so a conforming shell should exit
	# on an arg error, and the ERR should not be printed.
	atf_check -s not-exit:0 -o empty -e not-empty \
		${TEST_SH} -c 'set -% ; echo ERR'
}

atf_test_case Option_switching
Option_switching_head() {
	atf_set "descr" "options can be enabled and disabled"
}
Option_switching_body() {

	# Cannot test -m, setting it causes test shell to fail...
	# (test shell gets SIGKILL!)  Wonder why ... something related to atf
	# That is, it works if just run as "sh -c 'echo $-; set -m; echo $-'"

	# Don't bother testing toggling -n, once on, it stays on...
	# (and because the test fn refuses to allow us to try)

	# Cannot test -o or -c here, or the extension -s
	# they can only be used, not switched

	# these are the posix options, that all shells should implement
	test_option_on_off a b C e f h u v x      # m

	# and these are extensions that might not exist (non-fatal to test)
	# -i and -s (and -c) are posix options, but are not required to
	# be accessable via the "set" command, just the command line.
	# We allow for -i to work with set, as that makes some sense,
	# -c and -s do not.
	test_optional_on_off E i I p q V || true

	# Also test (some) option combinations ...
	# only testing posix options here, because it is easier...
	test_option_on_off aeu vx Ca aCefux
}

atf_init_test_cases() {
	# tests are run in order sort of names produces, so choose names wisely

	# this one tests turning on/off all the mandatory. and extra flags
	atf_add_test_case Option_switching
	# and this tests the NetBSD "local -" functionality in functions.
	atf_add_test_case restore_local_opts

	# no tests for	-m (no idea how to do that one)
	#		-I (no easy way to generate the EOF it ignores)
	#		-i (not sure how to test that one at the minute)
	#		-p (because we aren't going to run tests setuid)
	#		-V/-E (too much effort, and a real test would be huge)
	#		-c (because almost all the other tests test it anyway)
	#		-q (because, for now, I am lazy)
	#		-s (coming soon, hopefully)
	#		-o (really +o: again, hopefully soon)
	#		-o longname (again, just laziness, don't wait...)
	# 		-h/-b (because NetBSD doesn't implement them)
	atf_add_test_case set_a
	atf_add_test_case set_C
	atf_add_test_case set_e
	atf_add_test_case set_f
	atf_add_test_case set_n
	atf_add_test_case set_u
	atf_add_test_case set_v
	atf_add_test_case set_x

	atf_add_test_case vi_emacs_VE_toggle
	atf_add_test_case xx_bogus
}
