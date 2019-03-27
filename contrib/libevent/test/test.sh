#!/bin/sh

BACKENDS="EVPORT KQUEUE EPOLL DEVPOLL POLL SELECT WIN32"
TESTS="test-eof test-closed test-weof test-time test-changelist test-fdleak"
FAILED=no
TEST_OUTPUT_FILE=${TEST_OUTPUT_FILE:-/dev/null}
REGRESS_ARGS=${REGRESS_ARGS:-}

# /bin/echo is a little more likely to support -n than sh's builtin echo,
# printf is even more likely
if test "`printf %s hello 2>&1`" = "hello"
then
	ECHO_N="printf %s"
else
	if test -x /bin/echo
	then
		ECHO_N="/bin/echo -n"
	else
		ECHO_N="echo -n"
	fi
fi

if test "$TEST_OUTPUT_FILE" != "/dev/null"
then
	touch "$TEST_OUTPUT_FILE" || exit 1
fi

TEST_DIR=.
TEST_SRC_DIR=.

T=`echo "$0" | sed -e 's/test.sh$//'`
if test -x "$T/test-init"
then
	TEST_DIR="$T"
elif test -x "./test/test-init"
then
        TEST_DIR="./test"
fi
if test -f "$T/check-dumpevents.py"
then
	TEST_SRC_DIR="$T"
elif test -f "./test/check-dumpevents.py"
then
        TEST_SRC_DIR="./test"
fi

setup () {
	for i in $BACKENDS; do
		eval "EVENT_NO$i=yes; export EVENT_NO$i"
	done
	unset EVENT_EPOLL_USE_CHANGELIST
	unset EVENT_PRECISE_TIMER
}

announce () {
	echo "$@"
	echo "$@" >>"$TEST_OUTPUT_FILE"
}

announce_n () {
	$ECHO_N "$@"
	echo "$@" >>"$TEST_OUTPUT_FILE"
}


run_tests () {
	if $TEST_DIR/test-init 2>>"$TEST_OUTPUT_FILE" ;
	then
		true
	else
		announce Skipping test
		return
	fi
	for i in $TESTS; do
		announce_n " $i: "
		if $TEST_DIR/$i >>"$TEST_OUTPUT_FILE" ;
		then
			announce OKAY ;
		else
			announce FAILED ;
			FAILED=yes
		fi
	done
	announce_n " test-dumpevents: "
	if python2 -c 'import sys; assert(sys.version_info >= (2, 4))' 2>/dev/null && test -f $TEST_SRC_DIR/check-dumpevents.py; then
	    if $TEST_DIR/test-dumpevents | python2 $TEST_SRC_DIR/check-dumpevents.py >> "$TEST_OUTPUT_FILE" ;
	    then
	        announce OKAY ;
	    else
	        announce FAILED ;
	    fi
	else
	    # no python
	    if $TEST_DIR/test-dumpevents >/dev/null; then
	        announce "OKAY (output not checked)" ;
	    else
	        announce "FAILED (output not checked)" ;
	    fi
	fi

	test -x $TEST_DIR/regress || return
	announce_n " regress: "
	if test "$TEST_OUTPUT_FILE" = "/dev/null" ;
	then
		$TEST_DIR/regress --quiet $REGRESS_ARGS
	else
		$TEST_DIR/regress $REGRESS_ARGS >>"$TEST_OUTPUT_FILE"
	fi
	if test "$?" = "0" ;
	then
		announce OKAY ;
	else
		announce FAILED ;
		FAILED=yes
	fi

	announce_n " regress_debug: "
	if test "$TEST_OUTPUT_FILE" = "/dev/null" ;
	then
		EVENT_DEBUG_MODE=1 $TEST_DIR/regress --quiet $REGRESS_ARGS
	else
		EVENT_DEBUG_MODE=1 $TEST_DIR/regress $REGRESS_ARGS >>"$TEST_OUTPUT_FILE"
	fi
	if test "$?" = "0" ;
	then
		announce OKAY ;
	else
		announce FAILED ;
		FAILED=yes
	fi
}

do_test() {
	setup
	announce "$1 $2"
	unset EVENT_NO$1
	if test "$2" = "(changelist)" ; then
	    EVENT_EPOLL_USE_CHANGELIST=yes; export EVENT_EPOLL_USE_CHANGELIST
	elif test "$2" = "(timerfd)" ; then
	    EVENT_PRECISE_TIMER=1; export EVENT_PRECISE_TIMER
	elif test "$2" = "(timerfd+changelist)" ; then
	    EVENT_EPOLL_USE_CHANGELIST=yes; export EVENT_EPOLL_USE_CHANGELIST
	    EVENT_PRECISE_TIMER=1; export EVENT_PRECISE_TIMER
        fi

	run_tests
}

usage()
{
	cat <<EOL
  -b   - specify backends
  -t   - run timerfd test
  -c   - run changelist test
  -T   - run timerfd+changelist test
EOL
}
main()
{
	backends=$BACKENDS
	timerfd=0
	changelist=0
	timerfd_changelist=0

	while getopts "b:tcT" c; do
		case "$c" in
			b) backends="$OPTARG";;
			t) timerfd=1;;
			c) changelist=1;;
			T) timerfd_changelist=1;;
			?*) usage && exit 1;;
		esac
	done

	announce "Running tests:"

	[ $timerfd -eq 0 ] || do_test EPOLL "(timerfd)"
	[ $changelist -eq 0 ] || do_test EPOLL "(changelist)"
	[ $timerfd_changelist -eq 0 ] || do_test EPOLL "(timerfd+changelist)"
	for i in $backends; do
		do_test $i
	done

	if test "$FAILED" = "yes"; then
		exit 1
	fi
}
main "$@"
