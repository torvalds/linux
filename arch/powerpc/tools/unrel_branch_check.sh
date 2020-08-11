#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
# Copyright © 2016,2020 IBM Corporation
#
# This script checks the unrelocated code of a vmlinux for "suspicious"
# branches to relocated code (head_64.S code).

# Have Kbuild supply the path to objdump so we handle cross compilation.
objdump="$1"
vmlinux="$2"

#__end_interrupts should be located within the first 64K
kstart=0xc000000000000000
printf -v kend '0x%x' $(( kstart + 0x10000 ))

end_intr=0x$(
$objdump -R -d --start-address="$kstart" --stop-address="$kend" "$vmlinux" 2>/dev/null |
awk '$2 == "<__end_interrupts>:" { print $1 }'
)
if [ "$end_intr" = "0x" ]; then
	exit 0
fi

$objdump -R -D --no-show-raw-insn --start-address="$kstart" --stop-address="$end_intr" "$vmlinux" |
sed -E -n '
# match lines that start with a kernel address
/^c[0-9a-f]*:\s*b/ {
	# drop a target that we do not care about
	/\<__start_initialization_multiplatform>/d
	# drop branches via ctr or lr
	/\<b.?.?(ct|l)r/d
	# cope with some differences between Clang and GNU objdumps
	s/\<bt.?\s*[[:digit:]]+,/beq/
	s/\<bf.?\s*[[:digit:]]+,/bne/
	# tidy up
	s/\s0x/ /
	s/://
	# format for the loop below
	s/^(\S+)\s+(\S+)\s+(\S+)\s*(\S*).*$/\1:\2:0x\3:\4/
	# strip out condition registers
	s/:0xcr[0-7],/:0x/
	p
}' | {

all_good=true
while IFS=: read -r from branch to sym; do
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
