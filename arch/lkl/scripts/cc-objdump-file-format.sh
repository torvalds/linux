#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Print the C compiler output file format, as determined by objdump.
t=`mktemp` || exit 1
echo 'void foo(void) {}' | $CC -x c - -c -o "$t" \
	&& $OBJDUMP -p "$t" | awk '/file format/ {print $4}'
rm "$t"
