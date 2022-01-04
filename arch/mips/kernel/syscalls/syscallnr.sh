#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

in="$1"
out="$2"
my_abis=`echo "($3)" | tr ',' '|'`
prefix="$4"

fileguard=_UAPI_ASM_MIPS_`basename "$out" | sed \
	-e 'y/abcdefghijklmnopqrstuvwxyz/ABCDEFGHIJKLMNOPQRSTUVWXYZ/' \
	-e 's/[^A-Z0-9_]/_/g' -e 's/__/_/g'`
grep -E "^[0-9A-Fa-fXx]+[[:space:]]+${my_abis}" "$in" | sort -n | (
	printf "#ifndef %s\n" "${fileguard}"
	printf "#define %s\n" "${fileguard}"
	printf "\n"

	nxt=0
	while read nr abi name entry compat ; do
		nxt=$((nr+1))
	done

	printf "#define __NR_%s_Linux_syscalls\t%s\n" "${prefix}" "${nxt}"
	printf "\n"
	printf "#endif /* %s */" "${fileguard}"
	printf "\n"
) > "$out"
