#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

set -e
set -o pipefail

# To debug, uncomment the following line
# set -x

# Output from -fpatchable-function-entry can only vary on ppc64 elfv2, so this
# should not be invoked for other targets. Therefore we can pass in -m64 and
# -mabi explicitly, to take care of toolchains defaulting to other targets.

# Test whether the compile option -fpatchable-function-entry exists and
# generates appropriate code
echo "int func() { return 0; }" | \
    $* -m64 -mabi=elfv2 -S -x c -O2 -fpatchable-function-entry=2 - -o - 2> /dev/null | \
    grep -q "__patchable_function_entries"

# Test whether nops are generated after the local entry point
echo "int x; int func() { return x; }" | \
    $* -m64 -mabi=elfv2 -S -x c -O2 -fpatchable-function-entry=2 - -o - 2> /dev/null | \
    awk 'BEGIN { RS = ";" } /\.localentry.*nop.*\n[[:space:]]*nop/ { print $0 }' | \
    grep -q "func:"

exit 0
