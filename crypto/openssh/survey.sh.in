#!/bin/sh
#
# Copyright (c) 2004, 2005 Darren Tucker
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

host="@host@"
AWK="@AWK@"
CC="@CC@"
CPP="@CPP@"
CFLAGS="@CFLAGS@"
CPPFLAGS="@CPPFLAGS@"
LDFLAGS="@LDFLAGS@"
LIBS="@LIBS@"

# Note format:
# identifier: [data] CRCR

echo "openssh-survey-version: 1"
echo
echo "openssh-version: `./ssh -V 2>&1`"
echo
configinv=`$AWK '/^  \\\$.*configure/' config.log | sed 's/^  \\\$ //g'`
echo "configure-invocation: $configinv"
echo
echo "host: $host"
echo
echo "uname: `uname`"
echo
echo "uname-r: `uname -r`"
echo
echo "uname-m: `uname -m`"
echo
echo "uname-p: `uname -p`"
echo
echo "oslevel: `oslevel 2>/dev/null`"
echo
echo "oslevel-r: `oslevel -r 2>/dev/null`"
echo
echo "cc: $CC"
echo
echo "cflags: $CFLAGS"
echo
echo "cppflags: $CPPFLAGS"
echo
echo "ldflags: $LDFLAGS"
echo
echo "libs: $LIBS"
echo
echo "ccver-v: `$CC -v 2>&1 | sed '/^[ \t]*$/d'`"
echo
echo "ccver-V: `$CC -V 2>&1 | sed '/^[ \t]*$/d'`"
echo
echo "cppdefines:"
${CPP} -dM - </dev/null
echo
echo "config.h:"
egrep '#define|#undef' config.h
echo
