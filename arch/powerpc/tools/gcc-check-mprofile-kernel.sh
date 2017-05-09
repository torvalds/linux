#!/bin/bash

set -e
set -o pipefail

# To debug, uncomment the following line
# set -x

# Test whether the compile option -mprofile-kernel exists and generates
# profiling code (ie. a call to _mcount()).
echo "int func() { return 0; }" | \
    $* -S -x c -O2 -p -mprofile-kernel - -o - 2> /dev/null | \
    grep -q "_mcount"

# Test whether the notrace attribute correctly suppresses calls to _mcount().

echo -e "#include <linux/compiler.h>\nnotrace int func() { return 0; }" | \
    $* -S -x c -O2 -p -mprofile-kernel - -o - 2> /dev/null | \
    grep -q "_mcount" && \
    exit 1

echo "OK"
exit 0
