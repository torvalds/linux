#!/bin/awk -f
# SPDX-License-Identifier: GPL-2.0
# gen-sysreg.awk: arm64 sysreg header generator
#
# Usage: awk -f gen-sysreg.awk sysregs.txt

function block_current() {
	return __current_block[__current_block_depth];
}

# Log an error and terminate
function fatal(msg) {
	print "Error at " NR ": " msg > "/dev/stderr"

	printf "Current block nesting:"

	for (i = 0; i <= __current_block_depth; i++) {
		printf " " __current_block[i]
	}
	printf "\n"

	exit 1
}

# Enter a new block, setting the active block to @block
function block_push(block) {
	__current_block[++__current_block_depth] = block
}

# Exit a block, setting the active block to the parent block
function block_pop() {
	if (__current_block_depth == 0)
		fatal("error: block_pop() in root block")

	__current_block_depth--;
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
	printf "%-56s%s\n", "#define " name, val
}

# Print standard BITMASK/SHIFT/WIDTH CPP definitions for a field
function define_field(reg, field, msb, lsb) {
	define(reg "_" field, "GENMASK(" msb ", " lsb ")")
	define(reg "_" field "_MASK", "GENMASK(" msb ", " lsb ")")
	define(reg "_" field "_SHIFT", lsb)
	define(reg "_" field "_WIDTH", msb - lsb + 1)
}

# Print a field _SIGNED definition for a field
function define_field_sign(reg, field, sign) {
	define(reg "_" field "_SIGNED", sign)
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

	__current_block_depth = 0
	__current_block[__current_block_depth] = "Root"
}

END {
	if (__current_block_depth != 0)
		fatal("Missing terminator for " block_current() " block")

	print "#endif /* __ASM_SYSREG_DEFS_H */"
}

# skip blank lines and comment lines
/^$/ { next }
/^[\t ]*#/ { next }

$1 == "SysregFields" && block_current() == "Root" {
	block_push("SysregFields")

	expect_fields(2)

	reg = $2

	res0 = "UL(0)"
	res1 = "UL(0)"
	unkn = "UL(0)"

	next_bit = 63

	next
}

$1 == "EndSysregFields" && block_current() == "SysregFields" {
	expect_fields(1)
	if (next_bit > 0)
		fatal("Unspecified bits in " reg)

	define(reg "_RES0", "(" res0 ")")
	define(reg "_RES1", "(" res1 ")")
	define(reg "_UNKN", "(" unkn ")")
	print ""

	reg = null
	res0 = null
	res1 = null
	unkn = null

	block_pop()
	next
}

$1 == "Sysreg" && block_current() == "Root" {
	block_push("Sysreg")

	expect_fields(7)

	reg = $2
	op0 = $3
	op1 = $4
	crn = $5
	crm = $6
	op2 = $7

	res0 = "UL(0)"
	res1 = "UL(0)"
	unkn = "UL(0)"

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

$1 == "EndSysreg" && block_current() == "Sysreg" {
	expect_fields(1)
	if (next_bit > 0)
		fatal("Unspecified bits in " reg)

	if (res0 != null)
		define(reg "_RES0", "(" res0 ")")
	if (res1 != null)
		define(reg "_RES1", "(" res1 ")")
	if (unkn != null)
		define(reg "_UNKN", "(" unkn ")")
	if (res0 != null || res1 != null || unkn != null)
		print ""

	reg = null
	op0 = null
	op1 = null
	crn = null
	crm = null
	op2 = null
	res0 = null
	res1 = null
	unkn = null

	block_pop()
	next
}

# Currently this is effectivey a comment, in future we may want to emit
# defines for the fields.
($1 == "Fields" || $1 == "Mapping") && block_current() == "Sysreg" {
	expect_fields(2)

	if (next_bit != 63)
		fatal("Some fields already defined for " reg)

	print "/* For " reg " fields see " $2 " */"
	print ""

        next_bit = 0
	res0 = null
	res1 = null
	unkn = null

	next
}


$1 == "Res0" && (block_current() == "Sysreg" || block_current() == "SysregFields") {
	expect_fields(2)
	parse_bitdef(reg, "RES0", $2)
	field = "RES0_" msb "_" lsb

	res0 = res0 " | GENMASK_ULL(" msb ", " lsb ")"

	next
}

$1 == "Res1" && (block_current() == "Sysreg" || block_current() == "SysregFields") {
	expect_fields(2)
	parse_bitdef(reg, "RES1", $2)
	field = "RES1_" msb "_" lsb

	res1 = res1 " | GENMASK_ULL(" msb ", " lsb ")"

	next
}

$1 == "Unkn" && (block_current() == "Sysreg" || block_current() == "SysregFields") {
	expect_fields(2)
	parse_bitdef(reg, "UNKN", $2)
	field = "UNKN_" msb "_" lsb

	unkn = unkn " | GENMASK_ULL(" msb ", " lsb ")"

	next
}

$1 == "Field" && (block_current() == "Sysreg" || block_current() == "SysregFields") {
	expect_fields(3)
	field = $3
	parse_bitdef(reg, field, $2)

	define_field(reg, field, msb, lsb)
	print ""

	next
}

$1 == "Raz" && (block_current() == "Sysreg" || block_current() == "SysregFields") {
	expect_fields(2)
	parse_bitdef(reg, field, $2)

	next
}

$1 == "SignedEnum" && (block_current() == "Sysreg" || block_current() == "SysregFields") {
	block_push("Enum")

	expect_fields(3)
	field = $3
	parse_bitdef(reg, field, $2)

	define_field(reg, field, msb, lsb)
	define_field_sign(reg, field, "true")

	next
}

$1 == "UnsignedEnum" && (block_current() == "Sysreg" || block_current() == "SysregFields") {
	block_push("Enum")

	expect_fields(3)
	field = $3
	parse_bitdef(reg, field, $2)

	define_field(reg, field, msb, lsb)
	define_field_sign(reg, field, "false")

	next
}

$1 == "Enum" && (block_current() == "Sysreg" || block_current() == "SysregFields") {
	block_push("Enum")

	expect_fields(3)
	field = $3
	parse_bitdef(reg, field, $2)

	define_field(reg, field, msb, lsb)

	next
}

$1 == "EndEnum" && block_current() == "Enum" {
	expect_fields(1)

	field = null
	msb = null
	lsb = null
	print ""

	block_pop()
	next
}

/0b[01]+/ && block_current() == "Enum" {
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
