#!/bin/sh

# $FreeBSD$

usage()
{
    cat >&2 <<__EOF__
A harness for test cases in the DTrace test suite.

usage: $(basename $0) <testfile>
__EOF__
    exit 1
}

gettag()
{
    local tag

    tag=$(basename $1)
    tag=${tag#*.}
    tag=${tag%%[a-z.]*}
    echo $tag
}

runtest()
{
    local dflags exe exstatus pid retval status

    exstatus=0
    retval=0

    case $TFILE in
    drp.DTRACEDROP_*.d|err.*.d|tst.*.d)
        case $TFILE in
        drp.DTRACEDROP_*.d)
            dflags="-x droptags"
            tag=$(gettag "$TFILE")
            ;;
        err.D_*.d)
            exstatus=1
            dflags="-x errtags"
            tag=$(gettag "$TFILE")
            ;;
        err.*.d)
            exstatus=1
            ;;
        esac

        exe=${TFILE%.*}.exe
        if [ -f "$exe" -a -x "$exe" ]; then
            ./$exe &
            pid=$!
            dflags="$dflags ${pid}"
        fi

        dtrace -C -s "${TFILE}" $dflags >$STDOUT 2>$STDERR
        status=$?

        if [ $status -ne $exstatus ]; then
            ERRMSG="dtrace exited with status ${status}, expected ${exstatus}"
            retval=1
        elif [ -n "${tag}" ] && ! grep -Fq " [${tag}] " ${STDERR}; then
            ERRMSG="dtrace's error output did not contain expected tag ${tag}"
            retval=1
        fi

        if [ -n "$pid" ]; then
            kill -0 $pid >/dev/null 2>&1 && kill -9 $pid >/dev/null 2>&1
            wait
        fi
        ;;
    err.*.ksh|tst.*.ksh)
        expr "$TFILE" : 'err.*' >/dev/null && exstatus=1

        tst=$TFILE ksh "$TFILE" /usr/sbin/dtrace >$STDOUT 2>$STDERR
        status=$?

        if [ $status -ne $exstatus ]; then
            ERRMSG="script exited with status ${status}, expected ${exstatus}"
            retval=1
        fi
        ;;
    *)
        ERRMSG="unexpected test file name $TFILE"
        retval=1
        ;;
    esac

    if [ $retval -eq 0 ] && \
        head -n 1 $STDOUT | grep -q -E '^#!/.*ksh$'; then
        ksh $STDOUT
        retval=$?
    fi

    return $retval
}

[ $# -eq 1 ] || usage

readonly STDERR=$(mktemp)
readonly STDOUT=$(mktemp)
readonly TFILE=$(basename $1)
readonly EXOUT=${TFILE}.out

kldstat -q -m dtrace_test || kldload dtrace_test
cd $(dirname $1)
runtest
RESULT=$?

if [ $RESULT -eq 0 -a -f $EXOUT -a -r $EXOUT ] && \
   ! cmp $STDOUT $EXOUT >/dev/null 2>&1; then
    ERRMSG="test output mismatch"
    RESULT=1
fi

if [ $RESULT -ne 0 ]; then
    echo "test $TFILE failed: $ERRMSG" >&2
    if [ $(stat -f '%z' $STDOUT) -gt 0 ]; then
        cat >&2 <<__EOF__
test stdout:
--
$(cat $STDOUT)
--
__EOF__
    fi
    if [ $(stat -f '%z' $STDERR) -gt 0 ]; then
        cat >&2 <<__EOF__
test stderr:
--
$(cat $STDERR)
--
__EOF__
    fi
fi

rm -f $STDERR $STDOUT
exit $RESULT
