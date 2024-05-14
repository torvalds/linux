#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
# Copyright © 2016,2020 IBM Corporation
#
# This script checks the unrelocated code of a vmlinux for "suspicious"
# branches to relocated code (head_64.S code).

# Have Kbuild supply the path to objdump and nm so we handle cross compilation.
objdump="$1"
nm="$2"
vmlinux="$3"

kstart=0xc000000000000000

end_intr=0x$($nm -p "$vmlinux" |
	sed -E -n '/\s+[[:alpha:]]\s+__end_interrupts\s*$/{s///p;q}')
if [ "$end_intr" = "0x" ]; then
	exit 0
fi

# we know that there is a correct branch to
# __start_initialization_multiplatform, so find its address
# so we can exclude it.
sim=0x$($nm -p "$vmlinux" |
	sed -E -n '/\s+[[:alpha:]]\s+__start_initialization_multiplatform\s*$/{s///p;q}')

$objdump -D --no-show-raw-insn --start-address="$kstart" --stop-address="$end_intr" "$vmlinux" |
sed -E -n '
# match lines that start with a kernel address
/^c[0-9a-f]*:\s*b/ {
	# drop branches via ctr or lr
	/\<b.?.?(ct|l)r/d
	# cope with some differences between Clang and GNU objdumps
	s/\<bt.?\s*[[:digit:]]+,/beq/
	s/\<bf.?\s*[[:digit:]]+,/bne/
	# tidy up
	s/\s0x/ /
	s/://
	# format for the loop below
	s/^(\S+)\s+(\S+)\s+(\S+)\s*(\S*).*$/\1:\2:\3:\4/
	# strip out condition registers
	s/:cr[0-7],/:/
	p
}' | {

all_good=true
while IFS=: read -r from branch to sym; do
	case "$to" in
	c*)	to="0x$to"
		;;
	.+*)
		to=${to#.+}
		if [ "$branch" = 'b' ]; then
			if (( to >= 0x2000000 )); then
				to=$(( to - 0x4000000 ))
			fi
		elif (( to >= 0x8000 )); then
			to=$(( to - 0x10000 ))
		fi
		printf -v to '0x%x' $(( "0x$from" + to ))
		;;
	*)	printf 'Unkown branch format\n'
		;;
	esac
	if [ "$to" = "$sim" ]; then
		continue
	fi
	if (( to > end_intr )); then
		if $all_good; then
			printf '%s\n' 'WARNING: Unrelocated relative branches'
			all_good=false
		fi
		printf '%s %s-> %s %s\n' "$from" "$branch" "$to" "$sym"
	fi
done

$all_good

}
