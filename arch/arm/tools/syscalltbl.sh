#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
in="$1"
out="$2"
my_abis=`echo "($3)" | tr ',' '|'`

grep -E "^[0-9A-Fa-fXx]+[[:space:]]+${my_abis}" "$in" | sort -n | (
    while read nr abi name entry compat; do
        if [ "$abi" = "eabi" -a -n "$compat" ]; then
            echo "$in: error: a compat entry for an EABI syscall ($name) makes no sense" >&2
            exit 1
        fi

	if [ -n "$entry" ]; then
            if [ -z "$compat" ]; then
                echo "NATIVE($nr, $entry)"
            else
                echo "COMPAT($nr, $entry, $compat)"
            fi
        fi
    done
) > "$out"
