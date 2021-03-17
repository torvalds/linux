#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

in="$1"
out="$2"
my_abis=`echo "($3)" | tr ',' '|'`
prefix="$4"
offset="$5"

fileguard=_ASM_X86_`basename "$out" | sed \
    -e 'y/abcdefghijklmnopqrstuvwxyz/ABCDEFGHIJKLMNOPQRSTUVWXYZ/' \
    -e 's/[^A-Z0-9_]/_/g' -e 's/__/_/g'`
grep -E "^[0-9A-Fa-fXx]+[[:space:]]+${my_abis}" "$in" | sort -n | (
    echo "#ifndef ${fileguard}"
    echo "#define ${fileguard} 1"
    echo ""

    max=0
    while read nr abi name entry ; do
	if [ -z "$offset" ]; then
	    echo "#define __NR_${prefix}${name} $nr"
	else
	    echo "#define __NR_${prefix}${name} ($offset + $nr)"
        fi

	max=$nr
    done

    echo ""
    echo "#ifdef __KERNEL__"
    echo "#define __NR_${prefix}syscall_max $max"
    echo "#endif"
    echo ""
    echo "#endif /* ${fileguard} */"
) > "$out"
