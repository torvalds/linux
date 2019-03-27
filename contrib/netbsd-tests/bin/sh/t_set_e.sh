# $NetBSD: t_set_e.sh,v 1.4 2016/03/31 16:22:27 christos Exp $
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

# references:
#   http://www.opengroup.org/onlinepubs/009695399/utilities/set.html
#   http://www.opengroup.org/onlinepubs/009695399/utilities/xcu_chap02.html

# the implementation of "sh" to test
: ${TEST_SH:="/bin/sh"}

failwith()
{
	case "$SH_FAILS" in
		"") SH_FAILS=`echo "$1"`;;
		*) SH_FAILS="$SH_FAILS"`echo; echo "$1"`;;
	esac
}

check1()
{
	#echo "$TEST_SH -c $1"
	result=`$TEST_SH -c "$1" 2>/dev/null | tr '\n' ' ' | sed 's/ *$//'`
	if [ "$result" != "$2" ]; then
		MSG=`printf "%-56s %-8s  %s" "$3" "$result" "$2"`
		failwith "$MSG"
		failcount=`expr $failcount + 1`
	fi
	count=`expr $count + 1`
}

# direct check: try the given expression.
dcheck()
{
	check1 "$1" "$2" "$1"
}

# eval check: indirect through eval.
# as of this writing, this changes the behavior pretty drastically and
# is thus important to test. (PR bin/29861)
echeck()
{
	check1 'eval '"'( $1 )'" "$2" "eval '($1)'"
}

atf_test_case all
all_head() {
	atf_set "descr" "Tests that 'set -e' works correctly"
}
all_body() {
	count=0
	failcount=0

	# make sure exiting from a subshell behaves as expected
	dcheck '(set -e; exit 1; echo ERR$?); echo OK$?' 'OK1'
	echeck '(set -e; exit 1; echo ERR$?); echo OK$?' 'OK1'

	# first, check basic functioning.
	# The ERR shouldn't print; the result of the () should be 1.
	# Henceforth we'll assume that we don't need to check $?.
	dcheck '(set -e; false; echo ERR$?); echo OK$?' 'OK1'
	echeck '(set -e; false; echo ERR$?); echo OK$?' 'OK1'

	# these cases should be equivalent to the preceding.
	dcheck '(set -e; /nonexistent; echo ERR); echo OK' 'OK'
	echeck '(set -e; /nonexistent; echo ERR); echo OK' 'OK'
	dcheck '(set -e; nonexistent-program-on-path; echo ERR); echo OK' 'OK'
	echeck '(set -e; nonexistent-program-on-path; echo ERR); echo OK' 'OK'
	dcheck 'f() { false; }; (set -e; f; echo ERR); echo OK' 'OK'
	echeck 'f() { false; }; (set -e; f; echo ERR); echo OK' 'OK'
	dcheck 'f() { return 1; }; (set -e; f; echo ERR); echo OK' 'OK'
	echeck 'f() { return 1; }; (set -e; f; echo ERR); echo OK' 'OK'

	# but! with set -e, the false should cause an *immediate* exit.
	# The return form should not, as such, but there's no way to
	# distinguish it.
	dcheck 'f() { false; echo ERR; }; (set -e; f); echo OK' 'OK'
	echeck 'f() { false; echo ERR; }; (set -e; f); echo OK' 'OK'

	# set is not scoped, so these should not exit at all.
	dcheck 'f() { set +e; false; echo OK; }; (set -e; f); echo OK' 'OK OK'
	echeck 'f() { set +e; false; echo OK; }; (set -e; f); echo OK' 'OK OK'

	# according to the standard, only failing *simple* commands
	# cause an exit under -e. () is not a simple command.
	#   Correct (per POSIX):
	#dcheck '(set -e; (set +e; false; echo OK; false); echo OK)' 'OK OK'
	#echeck '(set -e; (set +e; false; echo OK; false); echo OK)' 'OK OK'
	#   Wrong current behavior:
	dcheck '(set -e; (set +e; false; echo OK; false); echo OK)' 'OK'
	echeck '(set -e; (set +e; false; echo OK; false); echo OK)' 'OK'

	# make sure an inner nested shell does exit though.
	dcheck '(set -e; (false; echo ERR)); echo OK' 'OK'

	# The left hand side of an || or && is explicitly tested and
	# thus should not cause an exit. Furthermore, because a || or
	# && expression is not a simple command, there should be no
	# exit even if the overall result is false.
	dcheck '(set -e; false || true; echo OK); echo OK' 'OK OK'
	echeck '(set -e; false || true; echo OK); echo OK' 'OK OK'
	dcheck '(set -e; false && true; echo OK); echo OK' 'OK OK'
	echeck '(set -e; false && true; echo OK); echo OK' 'OK OK'

	# However, the right hand side is not tested, so a failure
	# there *should* cause an exit, regardless of whether it
	# appears inside a non-simple command.
	#
	# Note that in at least one place the standard does not
	# distinguish between the left and right hand sides of
	# logical operators. It is possible that for strict
	# compliance these need to not exit; however, if so that
	# should probably be limited to when some strict-posix setting
	# is in effect and tested accordingly.
	#
	dcheck '(set -e; false || false; echo ERR); echo OK' 'OK'
	dcheck '(set -e; true && false; echo ERR); echo OK' 'OK'
	echeck '(set -e; false || false; echo ERR); echo OK' 'OK'
	echeck '(set -e; true && false; echo ERR); echo OK' 'OK'

	# correct:
	#dcheck '(set -e; false && false; echo ERR); echo OK' 'OK'
	#echeck '(set -e; false && false; echo ERR); echo OK' 'OK'

	# wrong current behavior:
	dcheck '(set -e; false && false; echo ERR); echo OK' 'ERR OK'
	echeck '(set -e; false && false; echo ERR); echo OK' 'ERR OK'

	# A failure that is not reached because of short-circuit
	# evaluation should not cause an exit, however.
	dcheck '(set -e; true || false; echo OK); echo OK' 'OK OK'
	echeck '(set -e; true || false; echo OK); echo OK' 'OK OK'

	# For completeness, test the other two combinations.
	dcheck '(set -e; true || true; echo OK); echo OK' 'OK OK'
	dcheck '(set -e; true && true; echo OK); echo OK' 'OK OK'
	echeck '(set -e; true || true; echo OK); echo OK' 'OK OK'
	echeck '(set -e; true && true; echo OK); echo OK' 'OK OK'

	# likewise, none of these should exit.
	dcheck '(set -e; while false; do :; done; echo OK); echo OK' 'OK OK'
	dcheck '(set -e; if false; then :; fi; echo OK); echo OK' 'OK OK'
	# problematic :-)
	#dcheck '(set -e; until false; do :; done; echo OK); echo OK' 'OK OK'
	dcheck '(set -e; until [ "$t" = 1 ]; do t=1; done; echo OK); echo OK' \
	  'OK OK'
	echeck '(set -e; while false; do :; done; echo OK); echo OK' 'OK OK'
	echeck '(set -e; if false; then :; fi; echo OK); echo OK' 'OK OK'
	echeck '(set -e; until [ "$t" = 1 ]; do t=1; done; echo OK); echo OK' \
	  'OK OK'

	# the bang operator tests its argument and thus the argument
	# should not cause an exit. it is also not a simple command (I
	# believe) so it also shouldn't exit even if it yields a false
	# result.
	dcheck '(set -e; ! false; echo OK); echo OK' 'OK OK'
	dcheck '(set -e; ! true; echo OK); echo OK' 'OK OK'
	echeck '(set -e; ! false; echo OK); echo OK' 'OK OK'
	echeck '(set -e; ! true; echo OK); echo OK' 'OK OK'

	# combined case with () and &&; the inner expression is false
	# but does not itself exit, and the () should not cause an 
	# exit even when failing.
	# correct:
	#dcheck '(set -e; (false && true); echo OK); echo OK' 'OK OK'
	#echeck '(set -e; (false && true); echo OK); echo OK' 'OK OK'
	# wrong current behavior:
	dcheck '(set -e; (false && true); echo OK); echo OK' 'OK'
	echeck '(set -e; (false && true); echo OK); echo OK' 'OK'

	# pipelines. only the right-hand end is significant.
	dcheck '(set -e; false | true; echo OK); echo OK' 'OK OK'
	echeck '(set -e; false | true; echo OK); echo OK' 'OK OK'
	dcheck '(set -e; true | false; echo ERR); echo OK' 'OK'
	echeck '(set -e; true | false; echo ERR); echo OK' 'OK'

	dcheck '(set -e; while true | false; do :; done; echo OK); echo OK' \
	    'OK OK'
	dcheck '(set -e; if true | false; then :; fi; echo OK); echo OK' \
	    'OK OK'


	# According to dsl@ in PR bin/32282, () is not defined as a
	# subshell, only as a grouping operator [and a scope, I guess]

	#		(This is incorrect.   () is definitely a sub-shell)

	# so the nested false ought to cause the whole shell to exit,
	# not just the subshell. dholland@ would like to see C&V,
	# because that seems like a bad idea. (Among other things, it
	# would break all the above test logic, which relies on being
	# able to isolate set -e behavior inside ().) However, I'm
	# going to put these tests here to make sure the issue gets
	# dealt with sometime.
	#
	# XXX: the second set has been disabled in the name of making
	# all tests "pass".
	#
	# As they should be, they are utter nonsense.

	# 1. error if the whole shell exits (current correct behavior)
	dcheck 'echo OK; (set -e; false); echo OK' 'OK OK'
	echeck 'echo OK; (set -e; false); echo OK' 'OK OK'
	# 2. error if the whole shell does not exit (dsl's suggested behavior)
	#dcheck 'echo OK; (set -e; false); echo ERR' 'OK'
	#echeck 'echo OK; (set -e; false); echo ERR' 'OK'

	# The current behavior of the shell is that it exits out as
	# far as -e is set and then stops. This is probably a
	# consequence of it handling () wrong, but it's a somewhat
	# curious compromise position between 1. and 2. above.
	dcheck '(set -e; (false; echo ERR); echo ERR); echo OK' 'OK'
	echeck '(set -e; (false; echo ERR); echo ERR); echo OK' 'OK'

	# backquote expansion (PR bin/17514)

	# (in-)correct
	#dcheck '(set -e; echo ERR `false`; echo ERR); echo OK' 'OK'
	#dcheck '(set -e; echo ERR $(false); echo ERR); echo OK' 'OK'
	#dcheck '(set -e; echo ERR `exit 3`; echo ERR); echo OK' 'OK'
	#dcheck '(set -e; echo ERR $(exit 3); echo ERR); echo OK' 'OK'
	# Not-wrong current behavior
	# the exit status of ommand substitution is ignored in most cases
	# None of these should be causing the shell to exit.
	dcheck '(set -e; echo ERR `false`; echo ERR); echo OK' 'ERR ERR OK'
	dcheck '(set -e; echo ERR $(false); echo ERR); echo OK' 'ERR ERR OK'
	dcheck '(set -e; echo ERR `exit 3`; echo ERR); echo OK' 'ERR ERR OK'
	dcheck '(set -e; echo ERR $(exit 3); echo ERR); echo OK' 'ERR ERR OK'

	# This is testing one case (the case?) where the exit status is used
	dcheck '(set -e; x=`false`; echo ERR); echo OK' 'OK'
	dcheck '(set -e; x=$(false); echo ERR); echo OK' 'OK'
	dcheck '(set -e; x=`exit 3`; echo ERR); echo OK' 'OK'
	dcheck '(set -e; x=$(exit 3); echo ERR); echo OK' 'OK'

	# correct (really just commented out incorrect nonsense)
	#echeck '(set -e; echo ERR `false`; echo ERR); echo OK' 'OK'
	#echeck '(set -e; echo ERR $(false); echo ERR); echo OK' 'OK'
	#echeck '(set -e; echo ERR `exit 3`; echo ERR); echo OK' 'OK'
	#echeck '(set -e; echo ERR $(exit 3); echo ERR); echo OK' 'OK'

	# not-wrong current behavior (as above)
	echeck '(set -e; echo ERR `false`; echo ERR); echo OK' 'ERR ERR OK'
	echeck '(set -e; echo ERR $(false); echo ERR); echo OK' 'ERR ERR OK'
	echeck '(set -e; echo ERR `exit 3`; echo ERR); echo OK' 'ERR ERR OK'
	echeck '(set -e; echo ERR $(exit 3); echo ERR); echo OK' 'ERR ERR OK'

	echeck '(set -e; x=`false`; echo ERR); echo OK' 'OK'
	echeck '(set -e; x=$(false); echo ERR); echo OK' 'OK'
	echeck '(set -e; x=`exit 3`; echo ERR); echo OK' 'OK'
	echeck '(set -e; x=$(exit 3); echo ERR); echo OK' 'OK'

	# shift (PR bin/37493)
	# correct
	# Actually, both ways are correct, both are permitted
	#dcheck '(set -e; shift || true; echo OK); echo OK' 'OK OK'
	#echeck '(set -e; shift || true; echo OK); echo OK' 'OK OK'
	# (not-) wrong current behavior
	#dcheck '(set -e; shift || true; echo OK); echo OK' 'OK'
	#echeck '(set -e; shift || true; echo OK); echo OK' 'OK'

	# what is wrong is this test assuming one behaviour or the other
	# (and incidentally this has nothing whatever to do with "-e",
	# the test should really be moved elsewhere...)
	# But for now, leave it here, and correct it:
	dcheck '(set -e; shift && echo OK); echo OK' 'OK'
	echeck '(set -e; shift && echo OK); echo OK' 'OK'

	# Done.

	if [ "x$SH_FAILS" != x ]; then
	    printf '%-56s %-8s  %s\n' "Expression" "Result" "Should be"
	    echo "$SH_FAILS"
	    atf_fail "$failcount of $count failed cases"
	else
	    atf_pass
	fi
}

atf_init_test_cases() {
	atf_add_test_case all
}
