#!/bin/sh

UNIT_BINARY="$1"
shift
UNIT_ARGS="$@"

test "x$OBJ" = "x" && OBJ=$PWD

# This mostly replicates the logic in test-exec.sh for running the
# regress tests under valgrind, except that we unconditionally enable
# leak checking because the unit tests should be clean.
VG_LEAK="--leak-check=full"
VG_TEST=`basename $UNIT_BINARY`
VG_LOG="$OBJ/valgrind-out/${VG_TEST}.%p"
VG_OPTS="--track-origins=yes $VG_LEAK --log-file=${VG_LOG}"
VG_OPTS="$VG_OPTS --trace-children=yes"
VG_PATH="valgrind"
if [ "x$VALGRIND_PATH" != "x" ]; then
	VG_PATH="$VALGRIND_PATH"
fi

exec $VG_PATH $VG_OPTS $UNIT_BINARY $UNIT_ARGS
