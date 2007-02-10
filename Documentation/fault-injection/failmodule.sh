#!/bin/bash
#
# Usage: failmodule <failname> <modulename> [stacktrace-depth]
#
#	<failname>: "failslab", "fail_alloc_page", or "fail_make_request"
#
#	<modulename>: module name that you want to inject faults.
#
#	[stacktrace-depth]: the maximum number of stacktrace walking allowed
#

STACKTRACE_DEPTH=5
if [ $# -gt 2 ]; then
	STACKTRACE_DEPTH=$3
fi

if [ ! -d /debug/$1 ]; then
	echo "Fault-injection $1 does not exist" >&2
	exit 1
fi
if [ ! -d /sys/module/$2 ]; then
	echo "Module $2 does not exist" >&2
	exit 1
fi

# Disable any fault injection
echo 0 > /debug/$1/stacktrace-depth

echo `cat /sys/module/$2/sections/.text` > /debug/$1/require-start
echo `cat /sys/module/$2/sections/.exit.text` > /debug/$1/require-end
echo $STACKTRACE_DEPTH > /debug/$1/stacktrace-depth
