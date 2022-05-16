#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

in="$1"
out="$2"
my_abis=`echo "($3)" | tr ',' '|'`
prefix="$4"
offset="$5"

fileguard=_UAPI_ASM_PARISC_`basename "$out" | sed \
	-e 'y/abcdefghijklmnopqrstuvwxyz/ABCDEFGHIJKLMNOPQRSTUVWXYZ/' \
	-e 's/[^A-Z0-9_]/_/g' -e 's/__/_/g'`
grep -E "^[0-9A-Fa-fXx]+[[:space:]]+${my_abis}" "$in" | sort -n | (
	printf "#ifndef %s\n" "${fileguard}"
	printf "#define %s\n" "${fileguard}"
	printf "\n"

	nxt=0
	while read nr abi name entry compat ; do
		if [ -z "$offset" ]; then
			printf "#define __NR_%s%s\t%s\n" \
				"${prefix}" "${name}" "${nr}"
		else
			printf "#define __NR_%s%s\t(%s + %s)\n" \
				"${prefix}" "${name}" "${offset}" "${nr}"
		fi
		nxt=$((nr+1))
	done

	printf "\n"
	printf "#ifdef __KERNEL__\n"
	printf "#define __NR_syscalls\t%s\n" "${nxt}"
	printf "#endif\n"
	printf "\n"
	printf "#endif /* %s */\n" "${fileguard}"
) > "$out"
