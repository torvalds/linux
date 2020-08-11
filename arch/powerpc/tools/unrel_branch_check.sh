#!/bin/bash
# Copyright © 2016 IBM Corporation
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version
# 2 of the License, or (at your option) any later version.
#
# This script checks the relocations of a vmlinux for "suspicious"
# branches from unrelocated code (head_64.S code).

# Turn this on if you want more debug output:
# set -x

# Have Kbuild supply the path to objdump so we handle cross compilation.
objdump="$1"
vmlinux="$2"

#__end_interrupts should be located within the first 64K
kstart=0xc000000000000000
printf -v kend '0x%x' $(( kstart + 0x10000 ))

end_intr=0x$(
$objdump -R -d --start-address="$kstart" --stop-address="$kend" "$vmlinux" |
awk '$2 == "<__end_interrupts>:" { print $1 }'
)

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
