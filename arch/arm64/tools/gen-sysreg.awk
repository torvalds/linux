#!/bin/awk -f
# SPDX-License-Identifier: GPL-2.0
# gen-sysreg.awk: arm64 sysreg header generator
#
# Usage: awk -f gen-sysreg.awk sysregs.txt

# Log an error and terminate
function fatal(msg) {
	print "Error at " NR ": " msg > "/dev/stderr"
	exit 1
}

# Sanity check that the start or end of a block makes sense at this point in
# the file. If not, produce an error and terminate.
#
# @this - the $Block or $EndBlock
# @prev - the only valid block to already be in (value of @block)
# @new - the new value of @block
function change_block(this, prev, new) {
	if (block != prev)
		fatal("unexpected " this " (inside " block ")")

	block = new
}

# Sanity check the number of records for a field makes sense. If not, produce
# an error and terminate.
function expect_fields(nf) {
	if (NF != nf)
		fatal(NF " fields found where " nf " expected")
}

# Print a CPP macro definition, padded with spaces so that the macro bodies
# line up in a column
function define(name, val) {
	printf "%-48s%s\n", "#define " name, val
}

# Print standard BITMASK/SHIFT/WIDTH CPP definitions for a field
function define_field(reg, field, msb, lsb) {
	define(reg "_" field, "GENMASK(" msb ", " lsb ")")
	define(reg "_" field "_MASK", "GENMASK(" msb ", " lsb ")")
	define(reg "_" field "_SHIFT", lsb)
	define(reg "_" field "_WIDTH", msb - lsb + 1)
}

# Parse a "<msb>[:<lsb>]" string into the global variables @msb and @lsb
function parse_bitdef(reg, field, bitdef, _bits)
{
	if (bitdef ~ /^[0-9]+$/) {
		msb = bitdef
		lsb = bitdef
	} else if (split(bitdef, _bits, ":") == 2) {
		msb = _bits[1]
		lsb = _bits[2]
	} else {
		fatal("invalid bit-range definition '" bitdef "'")
	}


	if (msb != next_bit)
		fatal(reg "." field " starts at " msb " not " next_bit)
	if (63 < msb || msb < 0)
		fatal(reg "." field " invalid high bit in '" bitdef "'")
	if (63 < lsb || lsb < 0)
		fatal(reg "." field " invalid low bit in '" bitdef "'")
	if (msb < lsb)
		fatal(reg "." field " invalid bit-range '" bitdef "'")
	if (low > high)
		fatal(reg "." field " has invalid range " high "-" low)

	next_bit = lsb - 1
}

BEGIN {
	print "#ifndef __ASM_SYSREG_DEFS_H"
	print "#define __ASM_SYSREG_DEFS_H"
	print ""
	print "/* Generated file - do not edit */"
	print ""

	block = "None"
}

END {
	print "#endif /* __ASM_SYSREG_DEFS_H */"
}

# skip blank lines and comment lines
/^$/ { next }
/^#/ { next }

/^SysregFields/ {
	change_block("SysregFields", "None", "SysregFields")
	expect_fields(2)

	reg = $2

	res0 = "UL(0)"
	res1 = "UL(0)"

	next_bit = 63

	next
}

/^EndSysregFields/ {
	if (next_bit > 0)
		fatal("Unspecified bits in " reg)

	change_block("EndSysregFields", "SysregFields", "None")

	define(reg "_RES0", "(" res0 ")")
	define(reg "_RES1", "(" res1 ")")
	print ""

	reg = null
	res0 = null
	res1 = null

	next
}

/^Sysreg/ {
	change_block("Sysreg", "None", "Sysreg")
	expect_fields(7)

	reg = $2
	op0 = $3
	op1 = $4
	crn = $5
	crm = $6
	op2 = $7

	res0 = "UL(0)"
	res1 = "UL(0)"

	define("REG_" reg, "S" op0 "_" op1 "_C" crn "_C" crm "_" op2)
	define("SYS_" reg, "sys_reg(" op0 ", " op1 ", " crn ", " crm ", " op2 ")")

	define("SYS_" reg "_Op0", op0)
	define("SYS_" reg "_Op1", op1)
	define("SYS_" reg "_CRn", crn)
	define("SYS_" reg "_CRm", crm)
	define("SYS_" reg "_Op2", op2)

	print ""

	next_bit = 63

	next
}

/^EndSysreg/ {
	if (next_bit > 0)
		fatal("Unspecified bits in " reg)

	change_block("EndSysreg", "Sysreg", "None")

	if (res0 != null)
		define(reg "_RES0", "(" res0 ")")
	if (res1 != null)
		define(reg "_RES1", "(" res1 ")")
	if (res0 != null || res1 != null)
		print ""

	reg = null
	op0 = null
	op1 = null
	crn = null
	crm = null
	op2 = null
	res0 = null
	res1 = null

	next
}

# Currently this is effectivey a comment, in future we may want to emit
# defines for the fields.
/^Fields/ && (block == "Sysreg") {
	expect_fields(2)

	if (next_bit != 63)
		fatal("Some fields already defined for " reg)

	print "/* For " reg " fields see " $2 " */"
	print ""

        next_bit = 0
	res0 = null
	res1 = null

	next
}


/^Res0/ && (block == "Sysreg" || block == "SysregFields") {
	expect_fields(2)
	parse_bitdef(reg, "RES0", $2)
	field = "RES0_" msb "_" lsb

	res0 = res0 " | GENMASK_ULL(" msb ", " lsb ")"

	next
}

/^Res1/ && (block == "Sysreg" || block == "SysregFields") {
	expect_fields(2)
	parse_bitdef(reg, "RES1", $2)
	field = "RES1_" msb "_" lsb

	res1 = res1 " | GENMASK_ULL(" msb ", " lsb ")"

	next
}

/^Field/ && (block == "Sysreg" || block == "SysregFields") {
	expect_fields(3)
	field = $3
	parse_bitdef(reg, field, $2)

	define_field(reg, field, msb, lsb)
	print ""

	next
}

/^Raz/ && (block == "Sysreg" || block == "SysregFields") {
	expect_fields(2)
	parse_bitdef(reg, field, $2)

	next
}

/^Enum/ {
	change_block("Enum", "Sysreg", "Enum")
	expect_fields(3)
	field = $3
	parse_bitdef(reg, field, $2)

	define_field(reg, field, msb, lsb)

	next
}

/^EndEnum/ {
	change_block("EndEnum", "Enum", "Sysreg")
	field = null
	msb = null
	lsb = null
	print ""
	next
}

/0b[01]+/ && block = "Enum" {
	expect_fields(2)
	val = $1
	name = $2

	define(reg "_" field "_" name, "UL(" val ")")
	next
}

# Any lines not handled by previous rules are unexpected
{
	fatal("unhandled statement")
}
