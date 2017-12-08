#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
in="$1"
out="$2"
my_abis=`echo "($3)" | tr ',' '|'`
align=1

fileguard=_ASM_ARM_`basename "$out" | sed \
    -e 'y/abcdefghijklmnopqrstuvwxyz/ABCDEFGHIJKLMNOPQRSTUVWXYZ/' \
    -e 's/[^A-Z0-9_]/_/g' -e 's/__/_/g'`

grep -E "^[0-9A-Fa-fXx]+[[:space:]]+${my_abis}" "$in" | sort -n | tail -n1 | (
    echo "#ifndef ${fileguard}
#define ${fileguard} 1

/*
 * This needs to be greater than __NR_last_syscall+1 in order to account
 * for the padding in the syscall table.
 */
"

    while read nr abi name entry; do
        nr=$(($nr + 1))
        while [ "$(($nr / (256 * $align) ))" -gt 0 ]; do
            align=$(( $align * 4 ))
        done
        nr=$(( ($nr + $align - 1) & ~($align - 1) ))
        echo "/* aligned to $align */"
        echo "#define __NR_syscalls $nr"
    done

    echo ""
    echo "#endif /* ${fileguard} */"
) > "$out"
