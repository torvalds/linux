#!/bin/awk -f
# gen-insn-attr-x86.awk: Instruction attribute table generator
# Written by Masami Hiramatsu <mhiramat@redhat.com>
#
# Usage: awk -f gen-insn-attr-x86.awk x86-opcode-map.txt > inat-tables.c

# Awk implementation sanity check
function check_awk_implement() {
	if (!match("abc", "[[:lower:]]+"))
		return "Your awk doesn't support charactor-class."
	if (sprintf("%x", 0) != "0")
		return "Your awk has a printf-format problem."
	return ""
}

# Clear working vars
function clear_vars() {
	delete table
	delete lptable2
	delete lptable1
	delete lptable3
	eid = -1 # escape id
	gid = -1 # group id
	aid = -1 # AVX id
	tname = ""
}

BEGIN {
	# Implementation error checking
	awkchecked = check_awk_implement()
	if (awkchecked != "") {
		print "Error: " awkchecked > "/dev/stderr"
		print "Please try to use gawk." > "/dev/stderr"
		exit 1
	}

	# Setup generating tables
	print "/* x86 opcode map generated from x86-opcode-map.txt */"
	print "/* Do not change this code. */\n"
	ggid = 1
	geid = 1
	gaid = 0
	delete etable
	delete gtable
	delete atable

	opnd_expr = "^[[:alpha:]/]"
	ext_expr = "^\\("
	sep_expr = "^\\|$"
	group_expr = "^Grp[[:alnum:]]+"

	imm_expr = "^[IJAO][[:lower:]]"
	imm_flag["Ib"] = "INAT_MAKE_IMM(INAT_IMM_BYTE)"
	imm_flag["Jb"] = "INAT_MAKE_IMM(INAT_IMM_BYTE)"
	imm_flag["Iw"] = "INAT_MAKE_IMM(INAT_IMM_WORD)"
	imm_flag["Id"] = "INAT_MAKE_IMM(INAT_IMM_DWORD)"
	imm_flag["Iq"] = "INAT_MAKE_IMM(INAT_IMM_QWORD)"
	imm_flag["Ap"] = "INAT_MAKE_IMM(INAT_IMM_PTR)"
	imm_flag["Iz"] = "INAT_MAKE_IMM(INAT_IMM_VWORD32)"
	imm_flag["Jz"] = "INAT_MAKE_IMM(INAT_IMM_VWORD32)"
	imm_flag["Iv"] = "INAT_MAKE_IMM(INAT_IMM_VWORD)"
	imm_flag["Ob"] = "INAT_MOFFSET"
	imm_flag["Ov"] = "INAT_MOFFSET"

	modrm_expr = "^([CDEGMNPQRSUVW/][[:lower:]]+|NTA|T[012])"
	force64_expr = "\\([df]64\\)"
	rex_expr = "^REX(\\.[XRWB]+)*"
	fpu_expr = "^ESC" # TODO

	lprefix1_expr = "\\(66\\)"
	lprefix2_expr = "\\(F3\\)"
	lprefix3_expr = "\\(F2\\)"
	max_lprefix = 4

	vexok_expr = "\\(VEX\\)"
	vexonly_expr = "\\(oVEX\\)"

	prefix_expr = "\\(Prefix\\)"
	prefix_num["Operand-Size"] = "INAT_PFX_OPNDSZ"
	prefix_num["REPNE"] = "INAT_PFX_REPNE"
	prefix_num["REP/REPE"] = "INAT_PFX_REPE"
	prefix_num["LOCK"] = "INAT_PFX_LOCK"
	prefix_num["SEG=CS"] = "INAT_PFX_CS"
	prefix_num["SEG=DS"] = "INAT_PFX_DS"
	prefix_num["SEG=ES"] = "INAT_PFX_ES"
	prefix_num["SEG=FS"] = "INAT_PFX_FS"
	prefix_num["SEG=GS"] = "INAT_PFX_GS"
	prefix_num["SEG=SS"] = "INAT_PFX_SS"
	prefix_num["Address-Size"] = "INAT_PFX_ADDRSZ"
	prefix_num["2bytes-VEX"] = "INAT_PFX_VEX2"
	prefix_num["3bytes-VEX"] = "INAT_PFX_VEX3"

	clear_vars()
}

function semantic_error(msg) {
	print "Semantic error at " NR ": " msg > "/dev/stderr"
	exit 1
}

function debug(msg) {
	print "DEBUG: " msg
}

function array_size(arr,   i,c) {
	c = 0
	for (i in arr)
		c++
	return c
}

/^Table:/ {
	print "/* " $0 " */"
	if (tname != "")
		semantic_error("Hit Table: before EndTable:.");
}

/^Referrer:/ {
	if (NF != 1) {
		# escape opcode table
		ref = ""
		for (i = 2; i <= NF; i++)
			ref = ref $i
		eid = escape[ref]
		tname = sprintf("inat_escape_table_%d", eid)
	}
}

/^AVXcode:/ {
	if (NF != 1) {
		# AVX/escape opcode table
		aid = $2
		if (gaid <= aid)
			gaid = aid + 1
		if (tname == "")	# AVX only opcode table
			tname = sprintf("inat_avx_table_%d", $2)
	}
	if (aid == -1 && eid == -1)	# primary opcode table
		tname = "inat_primary_table"
}

/^GrpTable:/ {
	print "/* " $0 " */"
	if (!($2 in group))
		semantic_error("No group: " $2 )
	gid = group[$2]
	tname = "inat_group_table_" gid
}

function print_table(tbl,name,fmt,n)
{
	print "const insn_attr_t " name " = {"
	for (i = 0; i < n; i++) {
		id = sprintf(fmt, i)
		if (tbl[id])
			print "	[" id "] = " tbl[id] ","
	}
	print "};"
}

/^EndTable/ {
	if (gid != -1) {
		# print group tables
		if (array_size(table) != 0) {
			print_table(table, tname "[INAT_GROUP_TABLE_SIZE]",
				    "0x%x", 8)
			gtable[gid,0] = tname
		}
		if (array_size(lptable1) != 0) {
			print_table(lptable1, tname "_1[INAT_GROUP_TABLE_SIZE]",
				    "0x%x", 8)
			gtable[gid,1] = tname "_1"
		}
		if (array_size(lptable2) != 0) {
			print_table(lptable2, tname "_2[INAT_GROUP_TABLE_SIZE]",
				    "0x%x", 8)
			gtable[gid,2] = tname "_2"
		}
		if (array_size(lptable3) != 0) {
			print_table(lptable3, tname "_3[INAT_GROUP_TABLE_SIZE]",
				    "0x%x", 8)
			gtable[gid,3] = tname "_3"
		}
	} else {
		# print primary/escaped tables
		if (array_size(table) != 0) {
			print_table(table, tname "[INAT_OPCODE_TABLE_SIZE]",
				    "0x%02x", 256)
			etable[eid,0] = tname
			if (aid >= 0)
				atable[aid,0] = tname
		}
		if (array_size(lptable1) != 0) {
			print_table(lptable1,tname "_1[INAT_OPCODE_TABLE_SIZE]",
				    "0x%02x", 256)
			etable[eid,1] = tname "_1"
			if (aid >= 0)
				atable[aid,1] = tname "_1"
		}
		if (array_size(lptable2) != 0) {
			print_table(lptable2,tname "_2[INAT_OPCODE_TABLE_SIZE]",
				    "0x%02x", 256)
			etable[eid,2] = tname "_2"
			if (aid >= 0)
				atable[aid,2] = tname "_2"
		}
		if (array_size(lptable3) != 0) {
			print_table(lptable3,tname "_3[INAT_OPCODE_TABLE_SIZE]",
				    "0x%02x", 256)
			etable[eid,3] = tname "_3"
			if (aid >= 0)
				atable[aid,3] = tname "_3"
		}
	}
	print ""
	clear_vars()
}

function add_flags(old,new) {
	if (old && new)
		return old " | " new
	else if (old)
		return old
	else
		return new
}

# convert operands to flags.
function convert_operands(opnd,       i,imm,mod)
{
	imm = null
	mod = null
	for (i in opnd) {
		i  = opnd[i]
		if (match(i, imm_expr) == 1) {
			if (!imm_flag[i])
				semantic_error("Unknown imm opnd: " i)
			if (imm) {
				if (i != "Ib")
					semantic_error("Second IMM error")
				imm = add_flags(imm, "INAT_SCNDIMM")
			} else
				imm = imm_flag[i]
		} else if (match(i, modrm_expr))
			mod = "INAT_MODRM"
	}
	return add_flags(imm, mod)
}

/^[0-9a-f]+\:/ {
	if (NR == 1)
		next
	# get index
	idx = "0x" substr($1, 1, index($1,":") - 1)
	if (idx in table)
		semantic_error("Redefine " idx " in " tname)

	# check if escaped opcode
	if ("escape" == $2) {
		if ($3 != "#")
			semantic_error("No escaped name")
		ref = ""
		for (i = 4; i <= NF; i++)
			ref = ref $i
		if (ref in escape)
			semantic_error("Redefine escape (" ref ")")
		escape[ref] = geid
		geid++
		table[idx] = "INAT_MAKE_ESCAPE(" escape[ref] ")"
		next
	}

	variant = null
	# converts
	i = 2
	while (i <= NF) {
		opcode = $(i++)
		delete opnds
		ext = null
		flags = null
		opnd = null
		# parse one opcode
		if (match($i, opnd_expr)) {
			opnd = $i
			split($(i++), opnds, ",")
			flags = convert_operands(opnds)
		}
		if (match($i, ext_expr))
			ext = $(i++)
		if (match($i, sep_expr))
			i++
		else if (i < NF)
			semantic_error($i " is not a separator")

		# check if group opcode
		if (match(opcode, group_expr)) {
			if (!(opcode in group)) {
				group[opcode] = ggid
				ggid++
			}
			flags = add_flags(flags, "INAT_MAKE_GROUP(" group[opcode] ")")
		}
		# check force(or default) 64bit
		if (match(ext, force64_expr))
			flags = add_flags(flags, "INAT_FORCE64")

		# check REX prefix
		if (match(opcode, rex_expr))
			flags = add_flags(flags, "INAT_MAKE_PREFIX(INAT_PFX_REX)")

		# check coprocessor escape : TODO
		if (match(opcode, fpu_expr))
			flags = add_flags(flags, "INAT_MODRM")

		# check VEX only code
		if (match(ext, vexonly_expr))
			flags = add_flags(flags, "INAT_VEXOK | INAT_VEXONLY")

		# check VEX only code
		if (match(ext, vexok_expr))
			flags = add_flags(flags, "INAT_VEXOK")

		# check prefixes
		if (match(ext, prefix_expr)) {
			if (!prefix_num[opcode])
				semantic_error("Unknown prefix: " opcode)
			flags = add_flags(flags, "INAT_MAKE_PREFIX(" prefix_num[opcode] ")")
		}
		if (length(flags) == 0)
			continue
		# check if last prefix
		if (match(ext, lprefix1_expr)) {
			lptable1[idx] = add_flags(lptable1[idx],flags)
			variant = "INAT_VARIANT"
		} else if (match(ext, lprefix2_expr)) {
			lptable2[idx] = add_flags(lptable2[idx],flags)
			variant = "INAT_VARIANT"
		} else if (match(ext, lprefix3_expr)) {
			lptable3[idx] = add_flags(lptable3[idx],flags)
			variant = "INAT_VARIANT"
		} else {
			table[idx] = add_flags(table[idx],flags)
		}
	}
	if (variant)
		table[idx] = add_flags(table[idx],variant)
}

END {
	if (awkchecked != "")
		exit 1
	# print escape opcode map's array
	print "/* Escape opcode map array */"
	print "const insn_attr_t const *inat_escape_tables[INAT_ESC_MAX + 1]" \
	      "[INAT_LSTPFX_MAX + 1] = {"
	for (i = 0; i < geid; i++)
		for (j = 0; j < max_lprefix; j++)
			if (etable[i,j])
				print "	["i"]["j"] = "etable[i,j]","
	print "};\n"
	# print group opcode map's array
	print "/* Group opcode map array */"
	print "const insn_attr_t const *inat_group_tables[INAT_GRP_MAX + 1]"\
	      "[INAT_LSTPFX_MAX + 1] = {"
	for (i = 0; i < ggid; i++)
		for (j = 0; j < max_lprefix; j++)
			if (gtable[i,j])
				print "	["i"]["j"] = "gtable[i,j]","
	print "};\n"
	# print AVX opcode map's array
	print "/* AVX opcode map array */"
	print "const insn_attr_t const *inat_avx_tables[X86_VEX_M_MAX + 1]"\
	      "[INAT_LSTPFX_MAX + 1] = {"
	for (i = 0; i < gaid; i++)
		for (j = 0; j < max_lprefix; j++)
			if (atable[i,j])
				print "	["i"]["j"] = "atable[i,j]","
	print "};"
}

