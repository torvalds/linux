#!/bin/awk -f
# SPDX-License-Identifier: GPL-2.0
# Usage: objdump -d a.out | awk -f distill.awk | ./test_get_len
# Distills the disassembly as follows:
# - Removes all lines except the disassembled instructions.
# - For instructions that exceed 1 line (7 bytes), crams all the hex bytes
# into a single line.
# - Remove bad(or prefix only) instructions

BEGIN {
	prev_addr = ""
	prev_hex = ""
	prev_mnemonic = ""
	bad_expr = "(\\(bad\\)|^rex|^.byte|^rep(z|nz)$|^lock$|^es$|^cs$|^ss$|^ds$|^fs$|^gs$|^data(16|32)$|^addr(16|32|64))"
	fwait_expr = "^9b "
	fwait_str="9b\tfwait"
}

/^ *[0-9a-f]+ <[^>]*>:/ {
	# Symbol entry
	printf("%s%s\n", $2, $1)
}

/^ *[0-9a-f]+:/ {
	if (split($0, field, "\t") < 3) {
		# This is a continuation of the same insn.
		prev_hex = prev_hex field[2]
	} else {
		# Skip bad instructions
		if (match(prev_mnemonic, bad_expr))
			prev_addr = ""
		# Split fwait from other f* instructions
		if (match(prev_hex, fwait_expr) && prev_mnemonic != "fwait") {
			printf "%s\t%s\n", prev_addr, fwait_str
			sub(fwait_expr, "", prev_hex)
		}
		if (prev_addr != "")
			printf "%s\t%s\t%s\n", prev_addr, prev_hex, prev_mnemonic
		prev_addr = field[1]
		prev_hex = field[2]
		prev_mnemonic = field[3]
	}
}

END {
	if (prev_addr != "")
		printf "%s\t%s\t%s\n", prev_addr, prev_hex, prev_mnemonic
}
