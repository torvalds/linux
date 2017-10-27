/*
 * Utility functions for x86 operand and address decoding
 *
 * Copyright (C) Intel Corporation 2017
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ratelimit.h>
#include <asm/inat.h>
#include <asm/insn.h>
#include <asm/insn-eval.h>
#include <asm/vm86.h>

#undef pr_fmt
#define pr_fmt(fmt) "insn: " fmt

enum reg_type {
	REG_TYPE_RM = 0,
	REG_TYPE_INDEX,
	REG_TYPE_BASE,
};

/**
 * is_string_insn() - Determine if instruction is a string instruction
 * @insn:	Instruction containing the opcode to inspect
 *
 * Returns:
 *
 * true if the instruction, determined by the opcode, is any of the
 * string instructions as defined in the Intel Software Development manual.
 * False otherwise.
 */
static bool is_string_insn(struct insn *insn)
{
	insn_get_opcode(insn);

	/* All string instructions have a 1-byte opcode. */
	if (insn->opcode.nbytes != 1)
		return false;

	switch (insn->opcode.bytes[0]) {
	case 0x6c ... 0x6f:	/* INS, OUTS */
	case 0xa4 ... 0xa7:	/* MOVS, CMPS */
	case 0xaa ... 0xaf:	/* STOS, LODS, SCAS */
		return true;
	default:
		return false;
	}
}

/**
 * get_seg_reg_override_idx() - obtain segment register override index
 * @insn:	Valid instruction with segment override prefixes
 *
 * Inspect the instruction prefixes in @insn and find segment overrides, if any.
 *
 * Returns:
 *
 * A constant identifying the segment register to use, among CS, SS, DS,
 * ES, FS, or GS. INAT_SEG_REG_DEFAULT is returned if no segment override
 * prefixes were found.
 *
 * -EINVAL in case of error.
 */
static int get_seg_reg_override_idx(struct insn *insn)
{
	int idx = INAT_SEG_REG_DEFAULT;
	int num_overrides = 0, i;

	insn_get_prefixes(insn);

	/* Look for any segment override prefixes. */
	for (i = 0; i < insn->prefixes.nbytes; i++) {
		insn_attr_t attr;

		attr = inat_get_opcode_attribute(insn->prefixes.bytes[i]);
		switch (attr) {
		case INAT_MAKE_PREFIX(INAT_PFX_CS):
			idx = INAT_SEG_REG_CS;
			num_overrides++;
			break;
		case INAT_MAKE_PREFIX(INAT_PFX_SS):
			idx = INAT_SEG_REG_SS;
			num_overrides++;
			break;
		case INAT_MAKE_PREFIX(INAT_PFX_DS):
			idx = INAT_SEG_REG_DS;
			num_overrides++;
			break;
		case INAT_MAKE_PREFIX(INAT_PFX_ES):
			idx = INAT_SEG_REG_ES;
			num_overrides++;
			break;
		case INAT_MAKE_PREFIX(INAT_PFX_FS):
			idx = INAT_SEG_REG_FS;
			num_overrides++;
			break;
		case INAT_MAKE_PREFIX(INAT_PFX_GS):
			idx = INAT_SEG_REG_GS;
			num_overrides++;
			break;
		/* No default action needed. */
		}
	}

	/* More than one segment override prefix leads to undefined behavior. */
	if (num_overrides > 1)
		return -EINVAL;

	return idx;
}

/**
 * check_seg_overrides() - check if segment override prefixes are allowed
 * @insn:	Valid instruction with segment override prefixes
 * @regoff:	Operand offset, in pt_regs, for which the check is performed
 *
 * For a particular register used in register-indirect addressing, determine if
 * segment override prefixes can be used. Specifically, no overrides are allowed
 * for rDI if used with a string instruction.
 *
 * Returns:
 *
 * True if segment override prefixes can be used with the register indicated
 * in @regoff. False if otherwise.
 */
static bool check_seg_overrides(struct insn *insn, int regoff)
{
	if (regoff == offsetof(struct pt_regs, di) && is_string_insn(insn))
		return false;

	return true;
}

/**
 * resolve_default_seg() - resolve default segment register index for an operand
 * @insn:	Instruction with opcode and address size. Must be valid.
 * @regs:	Register values as seen when entering kernel mode
 * @off:	Operand offset, in pt_regs, for which resolution is needed
 *
 * Resolve the default segment register index associated with the instruction
 * operand register indicated by @off. Such index is resolved based on defaults
 * described in the Intel Software Development Manual.
 *
 * Returns:
 *
 * If in protected mode, a constant identifying the segment register to use,
 * among CS, SS, ES or DS. If in long mode, INAT_SEG_REG_IGNORE.
 *
 * -EINVAL in case of error.
 */
static int resolve_default_seg(struct insn *insn, struct pt_regs *regs, int off)
{
	if (user_64bit_mode(regs))
		return INAT_SEG_REG_IGNORE;
	/*
	 * Resolve the default segment register as described in Section 3.7.4
	 * of the Intel Software Development Manual Vol. 1:
	 *
	 *  + DS for all references involving r[ABCD]X, and rSI.
	 *  + If used in a string instruction, ES for rDI. Otherwise, DS.
	 *  + AX, CX and DX are not valid register operands in 16-bit address
	 *    encodings but are valid for 32-bit and 64-bit encodings.
	 *  + -EDOM is reserved to identify for cases in which no register
	 *    is used (i.e., displacement-only addressing). Use DS.
	 *  + SS for rSP or rBP.
	 *  + CS for rIP.
	 */

	switch (off) {
	case offsetof(struct pt_regs, ax):
	case offsetof(struct pt_regs, cx):
	case offsetof(struct pt_regs, dx):
		/* Need insn to verify address size. */
		if (insn->addr_bytes == 2)
			return -EINVAL;

	case -EDOM:
	case offsetof(struct pt_regs, bx):
	case offsetof(struct pt_regs, si):
		return INAT_SEG_REG_DS;

	case offsetof(struct pt_regs, di):
		if (is_string_insn(insn))
			return INAT_SEG_REG_ES;
		return INAT_SEG_REG_DS;

	case offsetof(struct pt_regs, bp):
	case offsetof(struct pt_regs, sp):
		return INAT_SEG_REG_SS;

	case offsetof(struct pt_regs, ip):
		return INAT_SEG_REG_CS;

	default:
		return -EINVAL;
	}
}

/**
 * resolve_seg_reg() - obtain segment register index
 * @insn:	Instruction with operands
 * @regs:	Register values as seen when entering kernel mode
 * @regoff:	Operand offset, in pt_regs, used to deterimine segment register
 *
 * Determine the segment register associated with the operands and, if
 * applicable, prefixes and the instruction pointed by @insn.
 *
 * The segment register associated to an operand used in register-indirect
 * addressing depends on:
 *
 * a) Whether running in long mode (in such a case segments are ignored, except
 * if FS or GS are used).
 *
 * b) Whether segment override prefixes can be used. Certain instructions and
 *    registers do not allow override prefixes.
 *
 * c) Whether segment overrides prefixes are found in the instruction prefixes.
 *
 * d) If there are not segment override prefixes or they cannot be used, the
 *    default segment register associated with the operand register is used.
 *
 * The function checks first if segment override prefixes can be used with the
 * operand indicated by @regoff. If allowed, obtain such overridden segment
 * register index. Lastly, if not prefixes were found or cannot be used, resolve
 * the segment register index to use based on the defaults described in the
 * Intel documentation. In long mode, all segment register indexes will be
 * ignored, except if overrides were found for FS or GS. All these operations
 * are done using helper functions.
 *
 * The operand register, @regoff, is represented as the offset from the base of
 * pt_regs.
 *
 * As stated, the main use of this function is to determine the segment register
 * index based on the instruction, its operands and prefixes. Hence, @insn
 * must be valid. However, if @regoff indicates rIP, we don't need to inspect
 * @insn at all as in this case CS is used in all cases. This case is checked
 * before proceeding further.
 *
 * Please note that this function does not return the value in the segment
 * register (i.e., the segment selector) but our defined index. The segment
 * selector needs to be obtained using get_segment_selector() and passing the
 * segment register index resolved by this function.
 *
 * Returns:
 *
 * An index identifying the segment register to use, among CS, SS, DS,
 * ES, FS, or GS. INAT_SEG_REG_IGNORE is returned if running in long mode.
 *
 * -EINVAL in case of error.
 */
static int resolve_seg_reg(struct insn *insn, struct pt_regs *regs, int regoff)
{
	int idx;

	/*
	 * In the unlikely event of having to resolve the segment register
	 * index for rIP, do it first. Segment override prefixes should not
	 * be used. Hence, it is not necessary to inspect the instruction,
	 * which may be invalid at this point.
	 */
	if (regoff == offsetof(struct pt_regs, ip)) {
		if (user_64bit_mode(regs))
			return INAT_SEG_REG_IGNORE;
		else
			return INAT_SEG_REG_CS;
	}

	if (!insn)
		return -EINVAL;

	if (!check_seg_overrides(insn, regoff))
		return resolve_default_seg(insn, regs, regoff);

	idx = get_seg_reg_override_idx(insn);
	if (idx < 0)
		return idx;

	if (idx == INAT_SEG_REG_DEFAULT)
		return resolve_default_seg(insn, regs, regoff);

	/*
	 * In long mode, segment override prefixes are ignored, except for
	 * overrides for FS and GS.
	 */
	if (user_64bit_mode(regs)) {
		if (idx != INAT_SEG_REG_FS &&
		    idx != INAT_SEG_REG_GS)
			idx = INAT_SEG_REG_IGNORE;
	}

	return idx;
}

/**
 * get_segment_selector() - obtain segment selector
 * @regs:		Register values as seen when entering kernel mode
 * @seg_reg_idx:	Segment register index to use
 *
 * Obtain the segment selector from any of the CS, SS, DS, ES, FS, GS segment
 * registers. In CONFIG_X86_32, the segment is obtained from either pt_regs or
 * kernel_vm86_regs as applicable. In CONFIG_X86_64, CS and SS are obtained
 * from pt_regs. DS, ES, FS and GS are obtained by reading the actual CPU
 * registers. This done for only for completeness as in CONFIG_X86_64 segment
 * registers are ignored.
 *
 * Returns:
 *
 * Value of the segment selector, including null when running in
 * long mode.
 *
 * -EINVAL on error.
 */
static short get_segment_selector(struct pt_regs *regs, int seg_reg_idx)
{
#ifdef CONFIG_X86_64
	unsigned short sel;

	switch (seg_reg_idx) {
	case INAT_SEG_REG_IGNORE:
		return 0;
	case INAT_SEG_REG_CS:
		return (unsigned short)(regs->cs & 0xffff);
	case INAT_SEG_REG_SS:
		return (unsigned short)(regs->ss & 0xffff);
	case INAT_SEG_REG_DS:
		savesegment(ds, sel);
		return sel;
	case INAT_SEG_REG_ES:
		savesegment(es, sel);
		return sel;
	case INAT_SEG_REG_FS:
		savesegment(fs, sel);
		return sel;
	case INAT_SEG_REG_GS:
		savesegment(gs, sel);
		return sel;
	default:
		return -EINVAL;
	}
#else /* CONFIG_X86_32 */
	struct kernel_vm86_regs *vm86regs = (struct kernel_vm86_regs *)regs;

	if (v8086_mode(regs)) {
		switch (seg_reg_idx) {
		case INAT_SEG_REG_CS:
			return (unsigned short)(regs->cs & 0xffff);
		case INAT_SEG_REG_SS:
			return (unsigned short)(regs->ss & 0xffff);
		case INAT_SEG_REG_DS:
			return vm86regs->ds;
		case INAT_SEG_REG_ES:
			return vm86regs->es;
		case INAT_SEG_REG_FS:
			return vm86regs->fs;
		case INAT_SEG_REG_GS:
			return vm86regs->gs;
		case INAT_SEG_REG_IGNORE:
			/* fall through */
		default:
			return -EINVAL;
		}
	}

	switch (seg_reg_idx) {
	case INAT_SEG_REG_CS:
		return (unsigned short)(regs->cs & 0xffff);
	case INAT_SEG_REG_SS:
		return (unsigned short)(regs->ss & 0xffff);
	case INAT_SEG_REG_DS:
		return (unsigned short)(regs->ds & 0xffff);
	case INAT_SEG_REG_ES:
		return (unsigned short)(regs->es & 0xffff);
	case INAT_SEG_REG_FS:
		return (unsigned short)(regs->fs & 0xffff);
	case INAT_SEG_REG_GS:
		/*
		 * GS may or may not be in regs as per CONFIG_X86_32_LAZY_GS.
		 * The macro below takes care of both cases.
		 */
		return get_user_gs(regs);
	case INAT_SEG_REG_IGNORE:
		/* fall through */
	default:
		return -EINVAL;
	}
#endif /* CONFIG_X86_64 */
}

static int get_reg_offset(struct insn *insn, struct pt_regs *regs,
			  enum reg_type type)
{
	int regno = 0;

	static const int regoff[] = {
		offsetof(struct pt_regs, ax),
		offsetof(struct pt_regs, cx),
		offsetof(struct pt_regs, dx),
		offsetof(struct pt_regs, bx),
		offsetof(struct pt_regs, sp),
		offsetof(struct pt_regs, bp),
		offsetof(struct pt_regs, si),
		offsetof(struct pt_regs, di),
#ifdef CONFIG_X86_64
		offsetof(struct pt_regs, r8),
		offsetof(struct pt_regs, r9),
		offsetof(struct pt_regs, r10),
		offsetof(struct pt_regs, r11),
		offsetof(struct pt_regs, r12),
		offsetof(struct pt_regs, r13),
		offsetof(struct pt_regs, r14),
		offsetof(struct pt_regs, r15),
#endif
	};
	int nr_registers = ARRAY_SIZE(regoff);
	/*
	 * Don't possibly decode a 32-bit instructions as
	 * reading a 64-bit-only register.
	 */
	if (IS_ENABLED(CONFIG_X86_64) && !insn->x86_64)
		nr_registers -= 8;

	switch (type) {
	case REG_TYPE_RM:
		regno = X86_MODRM_RM(insn->modrm.value);
		if (X86_REX_B(insn->rex_prefix.value))
			regno += 8;
		break;

	case REG_TYPE_INDEX:
		regno = X86_SIB_INDEX(insn->sib.value);
		if (X86_REX_X(insn->rex_prefix.value))
			regno += 8;

		/*
		 * If ModRM.mod != 3 and SIB.index = 4 the scale*index
		 * portion of the address computation is null. This is
		 * true only if REX.X is 0. In such a case, the SIB index
		 * is used in the address computation.
		 */
		if (X86_MODRM_MOD(insn->modrm.value) != 3 && regno == 4)
			return -EDOM;
		break;

	case REG_TYPE_BASE:
		regno = X86_SIB_BASE(insn->sib.value);
		/*
		 * If ModRM.mod is 0 and SIB.base == 5, the base of the
		 * register-indirect addressing is 0. In this case, a
		 * 32-bit displacement follows the SIB byte.
		 */
		if (!X86_MODRM_MOD(insn->modrm.value) && regno == 5)
			return -EDOM;

		if (X86_REX_B(insn->rex_prefix.value))
			regno += 8;
		break;

	default:
		pr_err_ratelimited("invalid register type: %d\n", type);
		return -EINVAL;
	}

	if (regno >= nr_registers) {
		WARN_ONCE(1, "decoded an instruction with an invalid register");
		return -EINVAL;
	}
	return regoff[regno];
}

/**
 * insn_get_modrm_rm_off() - Obtain register in r/m part of the ModRM byte
 * @insn:	Instruction containing the ModRM byte
 * @regs:	Register values as seen when entering kernel mode
 *
 * Returns:
 *
 * The register indicated by the r/m part of the ModRM byte. The
 * register is obtained as an offset from the base of pt_regs. In specific
 * cases, the returned value can be -EDOM to indicate that the particular value
 * of ModRM does not refer to a register and shall be ignored.
 */
int insn_get_modrm_rm_off(struct insn *insn, struct pt_regs *regs)
{
	return get_reg_offset(insn, regs, REG_TYPE_RM);
}

/*
 * return the address being referenced be instruction
 * for rm=3 returning the content of the rm reg
 * for rm!=3 calculates the address using SIB and Disp
 */
void __user *insn_get_addr_ref(struct insn *insn, struct pt_regs *regs)
{
	int addr_offset, base_offset, indx_offset;
	unsigned long linear_addr = -1L;
	long eff_addr, base, indx;
	insn_byte_t sib;

	insn_get_modrm(insn);
	insn_get_sib(insn);
	sib = insn->sib.value;

	if (X86_MODRM_MOD(insn->modrm.value) == 3) {
		addr_offset = get_reg_offset(insn, regs, REG_TYPE_RM);
		if (addr_offset < 0)
			goto out;

		eff_addr = regs_get_register(regs, addr_offset);
	} else {
		if (insn->sib.nbytes) {
			/*
			 * Negative values in the base and index offset means
			 * an error when decoding the SIB byte. Except -EDOM,
			 * which means that the registers should not be used
			 * in the address computation.
			 */
			base_offset = get_reg_offset(insn, regs, REG_TYPE_BASE);
			if (base_offset == -EDOM)
				base = 0;
			else if (base_offset < 0)
				goto out;
			else
				base = regs_get_register(regs, base_offset);

			indx_offset = get_reg_offset(insn, regs, REG_TYPE_INDEX);

			if (indx_offset == -EDOM)
				indx = 0;
			else if (indx_offset < 0)
				goto out;
			else
				indx = regs_get_register(regs, indx_offset);

			eff_addr = base + indx * (1 << X86_SIB_SCALE(sib));
		} else {
			addr_offset = get_reg_offset(insn, regs, REG_TYPE_RM);
			if (addr_offset < 0)
				goto out;

			eff_addr = regs_get_register(regs, addr_offset);
		}

		eff_addr += insn->displacement.value;
	}

	linear_addr = (unsigned long)eff_addr;

out:
	return (void __user *)linear_addr;
}
