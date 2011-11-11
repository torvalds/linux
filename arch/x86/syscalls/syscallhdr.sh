#!/bin/sh

in="$1"
out="$2"
my_abis=`echo "$3" | tr ',' ' '`
prefix="$4"
offset="$5"

fileguard=_ASM_X86_`basename "$out" | sed \
    -e 'y/abcdefghijklmnopqrstuvwxyz/ABCDEFGHIJKLMNOPQRSTUVWXYZ/' \
    -e 's/[^A-Z0-9_]/_/g' -e 's/__/_/g'`

in_list () {
    local x
    for x in $1; do
	if [ x"$x" = x"$2" ]; then
	    return 0
	fi
    done
    return 1
}

grep '^[0-9]' "$in" | sort -n | (
    echo "#ifndef ${fileguard}"
    echo "#define ${fileguard} 1"
    echo ""

    while read nr abi name entry ; do
	if in_list "$my_abis" "$abi"; then
	    echo "#define __NR_${prefix}${name}" $((nr+offset))
        fi
    done

    echo ""
    echo "#endif /* ${fileguard} */"
) > "$out"
