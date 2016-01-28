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
	if [ "$abi" == "COMMON" -o "$abi" == "64" ]; then
	    # COMMON is the same as 64, except that we don't expect X32
	    # programs to use it.  Our expectation has nothing to do with
	    # any generated code, so treat them the same.
	    emit 64 "$nr" "$entry" "$compat"
	elif [ "$abi" == "X32" ]; then
	    # X32 is equivalent to 64 on an X32-compatible kernel.
	    echo "#ifdef CONFIG_X86_X32_ABI"
	    emit 64 "$nr" "$entry" "$compat"
	    echo "#endif"
	elif [ "$abi" == "I386" ]; then
	    emit "$abi" "$nr" "$entry" "$compat"
	else
	    echo "Unknown abi $abi" >&2
	    exit 1
	fi
    done
) > "$out"
