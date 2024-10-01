#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Check whether linker can handle cross-segment @segrel():
#
CPPFLAGS=""
CC=$1
OBJDUMP=$2
READELF=$3
dir=$(dirname $0)
tmp=${TMPDIR:-/tmp}
out=$tmp/out$$

# Check whether cross-segment segment-relative relocs work fine.  We need
# that for building the gate DSO:

$CC -nostdlib -static -Wl,-T$dir/check-segrel.lds $dir/check-segrel.S -o $out
res=$($OBJDUMP --full --section .rodata $out | fgrep 000 | cut -f3 -d' ')
rm -f $out
if [ $res != 00000a00 ]; then
    CPPFLAGS="$CPPFLAGS -DHAVE_BUGGY_SEGREL"
    cat >&2 <<EOF
warning: your linker cannot handle cross-segment segment-relative relocations.
         please upgrade to a newer version (it is safe to use this linker, but
         the kernel will be bigger than strictly necessary).
EOF
fi

# Check whether .align inside a function works as expected.

$CC -c $dir/check-text-align.S -o $out
$READELF -u $out | fgrep -q 'prologue(rlen=12)'
res=$?
rm -f $out
if [ $res -eq 0 ]; then
    CPPFLAGS="$CPPFLAGS -DHAVE_WORKING_TEXT_ALIGN"
fi

if ! $CC -c $dir/check-model.c -o $out 2>&1 | grep  __model__ | grep -q attrib
then
    CPPFLAGS="$CPPFLAGS -DHAVE_MODEL_SMALL_ATTRIBUTE"
fi
rm -f $out

# Check whether assembler supports .serialize.{data,instruction} directive.

$CC -c $dir/check-serialize.S -o $out 2>/dev/null
res=$?
rm -f $out
if [ $res -eq 0 ]; then
    CPPFLAGS="$CPPFLAGS -DHAVE_SERIALIZE_DIRECTIVE"
fi

echo $CPPFLAGS
