#!/bin/sh

in="$1"
out="$2"

emit() {
    abi="$1"
    nr="$2"
    entry="$3"
    compat="$4"
    if [ -n "$compat" ]; then
	echo "__SYSCALL_${abi}($nr, $entry, $compat)"
    elif [ -n "$entry" ]; then
	echo "__SYSCALL_${abi}($nr, $entry, $entry)"
    fi
}

grep '^[0-9]' "$in" | sort -n | (
    while read nr abi name entry compat; do
	abi=`echo "$abi" | tr '[a-z]' '[A-Z]'`
	emit "$abi" "$nr" "$entry" "$compat"
    done
) > "$out"
