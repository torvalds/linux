#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

in="$1"
out="$2"

syscall_macro() {
    local abi="$1"
    local nr="$2"
    local entry="$3"

    echo "__SYSCALL_${abi}($nr, $entry)"
}

emit() {
    local abi="$1"
    local nr="$2"
    local entry="$3"
    local compat="$4"

    if [ "$abi" != "I386" -a -n "$compat" ]; then
	echo "a compat entry ($abi: $compat) for a 64-bit syscall makes no sense" >&2
	exit 1
    fi

    if [ -z "$compat" ]; then
	if [ -n "$entry" ]; then
	    syscall_macro "$abi" "$nr" "$entry"
	fi
    else
	echo "#ifdef CONFIG_X86_32"
	if [ -n "$entry" ]; then
	    syscall_macro "$abi" "$nr" "$entry"
	fi
	echo "#else"
	syscall_macro "$abi" "$nr" "$compat"
	echo "#endif"
    fi
}

grep '^[0-9]' "$in" | sort -n | (
    while read nr abi name entry compat; do
	abi=`echo "$abi" | tr '[a-z]' '[A-Z]'`
	emit "$abi" "$nr" "$entry" "$compat"
    done
) > "$out"
