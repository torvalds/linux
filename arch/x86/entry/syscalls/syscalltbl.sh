#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

in="$1"
out="$2"

syscall_macro() {
    local abi="$1"
    local nr="$2"
    local entry="$3"

    # Entry can be either just a function name or "function/qualifier"
    real_entry="${entry%%/*}"
    if [ "$entry" = "$real_entry" ]; then
        qualifier=
    else
        qualifier=${entry#*/}
    fi

    echo "__SYSCALL_${abi}($nr, $real_entry, $qualifier)"
}

emit() {
    local abi="$1"
    local nr="$2"
    local entry="$3"
    local compat="$4"
    local umlentry=""

    if [ "$abi" != "I386" -a -n "$compat" ]; then
	echo "a compat entry ($abi: $compat) for a 64-bit syscall makes no sense" >&2
	exit 1
    fi

    # For CONFIG_UML, we need to strip the __x64_sys prefix
    if [ "$abi" = "64" -a "${entry}" != "${entry#__x64_sys}" ]; then
	    umlentry="sys${entry#__x64_sys}"
    fi

    if [ -z "$compat" ]; then
	if [ -n "$entry" -a -z "$umlentry" ]; then
	    syscall_macro "$abi" "$nr" "$entry"
	elif [ -n "$umlentry" ]; then # implies -n "$entry"
	    echo "#ifdef CONFIG_X86"
	    syscall_macro "$abi" "$nr" "$entry"
	    echo "#else /* CONFIG_UML */"
	    syscall_macro "$abi" "$nr" "$umlentry"
	    echo "#endif"
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
	if [ "$abi" = "COMMON" -o "$abi" = "64" ]; then
	    emit 64 "$nr" "$entry" "$compat"
	    if [ "$abi" = "COMMON" ]; then
		# COMMON means that this syscall exists in the same form for
		# 64-bit and X32.
		echo "#ifdef CONFIG_X86_X32_ABI"
		emit X32 "$nr" "$entry" "$compat"
		echo "#endif"
	    fi
	elif [ "$abi" = "X32" ]; then
	    echo "#ifdef CONFIG_X86_X32_ABI"
	    emit X32 "$nr" "$entry" "$compat"
	    echo "#endif"
	elif [ "$abi" = "I386" ]; then
	    emit "$abi" "$nr" "$entry" "$compat"
	else
	    echo "Unknown abi $abi" >&2
	    exit 1
	fi
    done
) > "$out"
