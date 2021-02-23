/*
 * Utility functions for x86 operand and address decoding
 *
 * Copyright (C) Intel Corporation 2017
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ratelimit.h>
#include <linux/mmu_context.h>
#include <asm/desc_defs.h>
#include <asm/desc.h>
#include <asm/inat.h>
#include <asm/insn.h>
#include <asm/insn-eval.h>
#include <asm/ldt.h>
#include <asm/vm86.h>

#undef pr_fmt
#define pr_fmt(fmt) "insn: " fmt

enum reg_type {
	REG_TYPE_RM = 0,
	REG_TYPE_REG,
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
 * insn_has_rep_prefix() - Determine if instruction has a REP prefix
 * @insn:	Instruction containing the prefix to inspect
 *
 * Returns:
 *
 * true if the instruction has a REP prefix, false if not.
 */
bool insn_has_rep_prefix(struct insn *insn)
{
	insn_byte_t p;
	int i;

	insn_get_prefixes(insn);

	for_each_insn_prefix(insn, i, p) {
		if (p == 0xf2 || p == 0xf3)
			return true;
	}

	return false;
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
	insn_byte_t p;

	insn_get_prefixes(insn);

	/* Look for any segment override prefixes. */
	for_each_insn_prefix(insn, i, p) {
		insn_attr_t attr;

		attr = inat_get_opcode_attribute(p);
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
	if (any_64bit_mode(regs))
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

		fallthrough;

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
		if (any_64bit_mode(regs))
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
	if (any_64bit_mode(regs)) {
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

		/*
		 * ModRM.mod == 0 and ModRM.rm == 5 means a 32-bit displacement
		 * follows the ModRM byte.
		 */
		if (!X86_MODRM_MOD(insn->modrm.value) && regno == 5)
			return -EDOM;

		if (X86_REX_B(insn->rex_prefix.value))
			regno += 8;
		break;

	case REG_TYPE_REG:
		regno = X86_MODRM_REG(insn->modrm.value);

		if (X86_REX_R(insn->rex_prefix.value))
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
 * get_reg_offset_16() - Obtain offset of register indicated by instruction
 * @insn:	Instruction containing ModRM byte
 * @regs:	Register values as seen when entering kernel mode
 * @offs1:	Offset of the first operand register
 * @offs2:	Offset of the second opeand register, if applicable
 *
 * Obtain the offset, in pt_regs, of the registers indicated by the ModRM byte
 * in @insn. This function is to be used with 16-bit address encodings. The
 * @offs1 and @offs2 will be written with the offset of the two registers
 * indicated by the instruction. In cases where any of the registers is not
 * referenced by the instruction, the value will be set to -EDOM.
 *
 * Returns:
 *
 * 0 on success, -EINVAL on error.
 */
static int get_reg_offset_16(struct insn *insn, struct pt_regs *regs,
			     int *offs1, int *offs2)
{
	/*
	 * 16-bit addressing can use one or two registers. Specifics of
	 * encodings are given in Table 2-1. "16-Bit Addressing Forms with the
	 * ModR/M Byte" of the Intel Software Development Manual.
	 */
	static const int regoff1[] = {
		offsetof(struct pt_regs, bx),
		offsetof(struct pt_regs, bx),
		offsetof(struct pt_regs, bp),
		offsetof(struct pt_regs, bp),
		offsetof(struct pt_regs, si),
		offsetof(struct pt_regs, di),
		offsetof(struct pt_regs, bp),
		offsetof(struct pt_regs, bx),
	};

	static const int regoff2[] = {
		offsetof(struct pt_regs, si),
		offsetof(struct pt_regs, di),
		offsetof(struct pt_regs, si),
		offsetof(struct pt_regs, di),
		-EDOM,
		-EDOM,
		-EDOM,
		-EDOM,
	};

	if (!offs1 || !offs2)
		return -EINVAL;

	/* Operand is a register, use the generic function. */
	if (X86_MODRM_MOD(insn->modrm.value) == 3) {
		*offs1 = insn_get_modrm_rm_off(insn, regs);
		*offs2 = -EDOM;
		return 0;
	}

	*offs1 = regoff1[X86_MODRM_RM(insn->modrm.value)];
	*offs2 = regoff2[X86_MODRM_RM(insn->modrm.value)];

	/*
	 * If ModRM.mod is 0 and ModRM.rm is 110b, then we use displacement-
	 * only addressing. This means that no registers are involved in
	 * computing the effective address. Thus, ensure that the first
	 * register offset is invalild. The second register offset is already
	 * invalid under the aforementioned conditions.
	 */
	if ((X86_MODRM_MOD(insn->modrm.value) == 0) &&
	    (X86_MODRM_RM(insn->modrm.value) == 6))
		*offs1 = -EDOM;

	return 0;
}

/**
 * get_desc() - Obtain contents of a segment descriptor
 * @out:	Segment descriptor contents on success
 * @sel:	Segment selector
 *
 * Given a segment selector, obtain a pointer to the segment descriptor.
 * Both global and local descriptor tables are supported.
 *
 * Returns:
 *
 * True on success, false on failure.
 *
 * NULL on error.
 */
static bool get_desc(struct desc_struct *out, unsigned short sel)
{
	struct desc_ptr gdt_desc = {0, 0};
	unsigned long desc_base;

#ifdef CONFIG_MODIFY_LDT_SYSCALL
	if ((sel & SEGMENT_TI_MASK) == SEGMENT_LDT) {
		bool success = false;
		struct ldt_struct *ldt;

		/* Bits [15:3] contain the index of the desired entry. */
		sel >>= 3;

		mutex_lock(&current->active_mm->context.lock);
		ldt = current->active_mm->context.ldt;
		if (ldt && sel < ldt->nr_entries) {
			*out = ldt->entries[sel];
			success = true;
		}

		mutex_unlock(&current->active_mm->context.lock);

		return success;
	}
#endif
	native_store_gdt(&gdt_desc);

	/*
	 * Segment descriptors have a size of 8 bytes. Thus, the index is
	 * multiplied by 8 to obtain the memory offset of the desired descriptor
	 * from the base of the GDT. As bits [15:3] of the segment selector
	 * contain the index, it can be regarded as multiplied by 8 already.
	 * All that remains is to clear bits [2:0].
	 */
	desc_base = sel & ~(SEGMENT_RPL_MASK | SEGMENT_TI_MASK);

	if (desc_base > gdt_desc.size)
		return false;

	*out = *(struct desc_struct *)(gdt_desc.address + desc_base);
	return true;
}

/**
 * insn_get_seg_base() - Obtain base address of segment descriptor.
 * @regs:		Register values as seen when entering kernel mode
 * @seg_reg_idx:	Index of the segment register pointing to seg descriptor
 *
 * Obtain the base address of the segment as indicated by the segment descriptor
 * pointed by the segment selector. The segment selector is obtained from the
 * input segment register index @seg_reg_idx.
 *
 * Returns:
 *
 * In protected mode, base address of the segment. Zero in long mode,
 * except when FS or GS are used. In virtual-8086 mode, the segment
 * selector shifted 4 bits to the right.
 *
 * -1L in case of error.
 */
unsigned long insn_get_seg_base(struct pt_regs *regs, int seg_reg_idx)
{
	struct desc_struct desc;
	short sel;

	sel = get_segment_selector(regs, seg_reg_idx);
	if (sel < 0)
		return -1L;

	if (v8086_mode(regs))
		/*
		 * Base is simply the segment selector shifted 4
		 * bits to the right.
		 */
		return (unsigned long)(sel << 4);

	if (any_64bit_mode(regs)) {
		/*
		 * Only FS or GS will have a base address, the rest of
		 * the segments' bases are forced to 0.
		 */
		unsigned long base;

		if (seg_reg_idx == INAT_SEG_REG_FS) {
			rdmsrl(MSR_FS_BASE, base);
		} else if (seg_reg_idx == INAT_SEG_REG_GS) {
			/*
			 * swapgs was called at the kernel entry point. Thus,
			 * MSR_KERNEL_GS_BASE will have the user-space GS base.
			 */
			if (user_mode(regs))
				rdmsrl(MSR_KERNEL_GS_BASE, base);
			else
				rdmsrl(MSR_GS_BASE, base);
		} else {
			base = 0;
		}
		return base;
	}

	/* In protected mode the segment selector cannot be null. */
	if (!sel)
		return -1L;

	if (!get_desc(&desc, sel))
		return -1L;

	return get_desc_base(&desc);
}

/**
 * get_seg_limit() - Obtain the limit of a segment descriptor
 * @regs:		Register values as seen when entering kernel mode
 * @seg_reg_idx:	Index of the segment register pointing to seg descriptor
 *
 * Obtain the limit of the segment as indicated by the segment descriptor
 * pointed by the segment selector. The segment selector is obtained from the
 * input segment register index @seg_reg_idx.
 *
 * Returns:
 *
 * In protected mode, the limit of the segment descriptor in bytes.
 * In long mode and virtual-8086 mode, segment limits are not enforced. Thus,
 * limit is returned as -1L to imply a limit-less segment.
 *
 * Zero is returned on error.
 */
static unsigned long get_seg_limit(struct pt_regs *regs, int seg_reg_idx)
{
	struct desc_struct desc;
	unsigned long limit;
	short sel;

	sel = get_segment_selector(regs, seg_reg_idx);
	if (sel < 0)
		return 0;

	if (any_64bit_mode(regs) || v8086_mode(regs))
		return -1L;

	if (!sel)
		return 0;

	if (!get_desc(&desc, sel))
		return 0;

	/*
	 * If the granularity bit is set, the limit is given in multiples
	 * of 4096. This also means that the 12 least significant bits are
	 * not tested when checking the segment limits. In practice,
	 * this means that the segment ends in (limit << 12) + 0xfff.
	 */
	limit = get_desc_limit(&desc);
	if (desc.g)
		limit = (limit << 12) + 0xfff;

	return limit;
}

/**
 * insn_get_code_seg_params() - Obtain code segment parameters
 * @regs:	Structure with register values as seen when entering kernel mode
 *
 * Obtain address and operand sizes of the code segment. It is obtained from the
 * selector contained in the CS register in regs. In protected mode, the default
 * address is determined by inspecting the L and D bits of the segment
 * descriptor. In virtual-8086 mode, the default is always two bytes for both
 * address and operand sizes.
 *
 * Returns:
 *
 * An int containing ORed-in default parameters on success.
 *
 * -EINVAL on error.
 */
int insn_get_code_seg_params(struct pt_regs *regs)
{
	struct desc_struct desc;
	short sel;

	if (v8086_mode(regs))
		/* Address and operand size are both 16-bit. */
		return INSN_CODE_SEG_PARAMS(2, 2);

	sel = get_segment_selector(regs, INAT_SEG_REG_CS);
	if (sel < 0)
		return sel;

	if (!get_desc(&desc, sel))
		return -EINVAL;

	/*
	 * The most significant byte of the Type field of the segment descriptor
	 * determines whether a segment contains data or code. If this is a data
	 * segment, return error.
	 */
	if (!(desc.type & BIT(3)))
		return -EINVAL;

	switch ((desc.l << 1) | desc.d) {
	case 0: /*
		 * Legacy mode. CS.L=0, CS.D=0. Address and operand size are
		 * both 16-bit.
		 */
		return INSN_CODE_SEG_PARAMS(2, 2);
	case 1: /*
		 * Legacy mode. CS.L=0, CS.D=1. Address and operand size are
		 * both 32-bit.
		 */
		return INSN_CODE_SEG_PARAMS(4, 4);
	case 2: /*
		 * IA-32e 64-bit mode. CS.L=1, CS.D=0. Address size is 64-bit;
		 * operand size is 32-bit.
		 */
		return INSN_CODE_SEG_PARAMS(4, 8);
	case 3: /* Invalid setting. CS.L=1, CS.D=1 */
		fallthrough;
	default:
		return -EINVAL;
	}
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

/**
 * insn_get_modrm_reg_off() - Obtain register in reg part of the ModRM byte
 * @insn:	Instruction containing the ModRM byte
 * @regs:	Register values as seen when entering kernel mode
 *
 * Returns:
 *
 * The register indicated by the reg part of the ModRM byte. The
 * register is obtained as an offset from the base of pt_regs.
 */
int insn_get_modrm_reg_off(struct insn *insn, struct pt_regs *regs)
{
	return get_reg_offset(insn, regs, REG_TYPE_REG);
}

/**
 * get_seg_base_limit() - obtain base address and limit of a segment
 * @insn:	Instruction. Must be valid.
 * @regs:	Register values as seen when entering kernel mode
 * @regoff:	Operand offset, in pt_regs, used to resolve segment descriptor
 * @base:	Obtained segment base
 * @limit:	Obtained segment limit
 *
 * Obtain the base address and limit of the segment associated with the operand
 * @regoff and, if any or allowed, override prefixes in @insn. This function is
 * different from insn_get_seg_base() as the latter does not resolve the segment
 * associated with the instruction operand. If a limit is not needed (e.g.,
 * when running in long mode), @limit can be NULL.
 *
 * Returns:
 *
 * 0 on success. @base and @limit will contain the base address and of the
 * resolved segment, respectively.
 *
 * -EINVAL on error.
 */
static int get_seg_base_limit(struct insn *insn, struct pt_regs *regs,
			      int regoff, unsigned long *base,
			      unsigned long *limit)
{
	int seg_reg_idx;

	if (!base)
		return -EINVAL;

	seg_reg_idx = resolve_seg_reg(insn, regs, regoff);
	if (seg_reg_idx < 0)
		return seg_reg_idx;

	*base = insn_get_seg_base(regs, seg_reg_idx);
	if (*base == -1L)
		return -EINVAL;

	if (!limit)
		return 0;

	*limit = get_seg_limit(regs, seg_reg_idx);
	if (!(*limit))
		return -EINVAL;

	return 0;
}

/**
 * get_eff_addr_reg() - Obtain effective address from register operand
 * @insn:	Instruction. Must be valid.
 * @regs:	Register values as seen when entering kernel mode
 * @regoff:	Obtained operand offset, in pt_regs, with the effective address
 * @eff_addr:	Obtained effective address
 *
 * Obtain the effective address stored in the register operand as indicated by
 * the ModRM byte. This function is to be used only with register addressing
 * (i.e.,  ModRM.mod is 3). The effective address is saved in @eff_addr. The
 * register operand, as an offset from the base of pt_regs, is saved in @regoff;
 * such offset can then be used to resolve the segment associated with the
 * operand. This function can be used with any of the supported address sizes
 * in x86.
 *
 * Returns:
 *
 * 0 on success. @eff_addr will have the effective address stored in the
 * operand indicated by ModRM. @regoff will have such operand as an offset from
 * the base of pt_regs.
 *
 * -EINVAL on error.
 */
static int get_eff_addr_reg(struct insn *insn, struct pt_regs *regs,
			    int *regoff, long *eff_addr)
{
	insn_get_modrm(insn);

	if (!insn->modrm.nbytes)
		return -EINVAL;

	if (X86_MODRM_MOD(insn->modrm.value) != 3)
		return -EINVAL;

	*regoff = get_reg_offset(insn, regs, REG_TYPE_RM);
	if (*regoff < 0)
		return -EINVAL;

	/* Ignore bytes that are outside the address size. */
	if (insn->addr_bytes == 2)
		*eff_addr = regs_get_register(regs, *regoff) & 0xffff;
	else if (insn->addr_bytes == 4)
		*eff_addr = regs_get_register(regs, *regoff) & 0xffffffff;
	else /* 64-bit address */
		*eff_addr = regs_get_register(regs, *regoff);

	return 0;
}

/**
 * get_eff_addr_modrm() - Obtain referenced effective address via ModRM
 * @insn:	Instruction. Must be valid.
 * @regs:	Register values as seen when entering kernel mode
 * @regoff:	Obtained operand offset, in pt_regs, associated with segment
 * @eff_addr:	Obtained effective address
 *
 * Obtain the effective address referenced by the ModRM byte of @insn. After
 * identifying the registers involved in the register-indirect memory reference,
 * its value is obtained from the operands in @regs. The computed address is
 * stored @eff_addr. Also, the register operand that indicates the associated
 * segment is stored in @regoff, this parameter can later be used to determine
 * such segment.
 *
 * Returns:
 *
 * 0 on success. @eff_addr will have the referenced effective address. @regoff
 * will have a register, as an offset from the base of pt_regs, that can be used
 * to resolve the associated segment.
 *
 * -EINVAL on error.
 */
static int get_eff_addr_modrm(struct insn *insn, struct pt_regs *regs,
			      int *regoff, long *eff_addr)
{
	long tmp;

	if (insn->addr_bytes != 8 && insn->addr_bytes != 4)
		return -EINVAL;

	insn_get_modrm(insn);

	if (!insn->modrm.nbytes)
		return -EINVAL;

	if (X86_MODRM_MOD(insn->modrm.value) > 2)
		return -EINVAL;

	*regoff = get_reg_offset(insn, regs, REG_TYPE_RM);

	/*
	 * -EDOM means that we must ignore the address_offset. In such a case,
	 * in 64-bit mode the effective address relative to the rIP of the
	 * following instruction.
	 */
	if (*regoff == -EDOM) {
		if (any_64bit_mode(regs))
			tmp = regs->ip + insn->length;
		else
			tmp = 0;
	} else if (*regoff < 0) {
		return -EINVAL;
	} else {
		tmp = regs_get_register(regs, *regoff);
	}

	if (insn->addr_bytes == 4) {
		int addr32 = (int)(tmp & 0xffffffff) + insn->displacement.value;

		*eff_addr = addr32 & 0xffffffff;
	} else {
		*eff_addr = tmp + insn->displacement.value;
	}

	return 0;
}

/**
 * get_eff_addr_modrm_16() - Obtain referenced effective address via ModRM
 * @insn:	Instruction. Must be valid.
 * @regs:	Register values as seen when entering kernel mode
 * @regoff:	Obtained operand offset, in pt_regs, associated with segment
 * @eff_addr:	Obtained effective address
 *
 * Obtain the 16-bit effective address referenced by the ModRM byte of @insn.
 * After identifying the registers involved in the register-indirect memory
 * reference, its value is obtained from the operands in @regs. The computed
 * address is stored @eff_addr. Also, the register operand that indicates
 * the associated segment is stored in @regoff, this parameter can later be used
 * to determine such segment.
 *
 * Returns:
 *
 * 0 on success. @eff_addr will have the referenced effective address. @regoff
 * will have a register, as an offset from the base of pt_regs, that can be used
 * to resolve the associated segment.
 *
 * -EINVAL on error.
 */
static int get_eff_addr_modrm_16(struct insn *insn, struct pt_regs *regs,
				 int *regoff, short *eff_addr)
{
	int addr_offset1, addr_offset2, ret;
	short addr1 = 0, addr2 = 0, displacement;

	if (insn->addr_bytes != 2)
		return -EINVAL;

	insn_get_modrm(insn);

	if (!insn->modrm.nbytes)
		return -EINVAL;

	if (X86_MODRM_MOD(insn->modrm.value) > 2)
		return -EINVAL;

	ret = get_reg_offset_16(insn, regs, &addr_offset1, &addr_offset2);
	if (ret < 0)
		return -EINVAL;

	/*
	 * Don't fail on invalid offset values. They might be invalid because
	 * they cannot be used for this particular value of ModRM. Instead, use
	 * them in the computation only if they contain a valid value.
	 */
	if (addr_offset1 != -EDOM)
		addr1 = regs_get_register(regs, addr_offset1) & 0xffff;

	if (addr_offset2 != -EDOM)
		addr2 = regs_get_register(regs, addr_offset2) & 0xffff;

	displacement = insn->displacement.value & 0xffff;
	*eff_addr = addr1 + addr2 + displacement;

	/*
	 * The first operand register could indicate to use of either SS or DS
	 * registers to obtain the segment selector.  The second operand
	 * register can only indicate the use of DS. Thus, the first operand
	 * will be used to obtain the segment selector.
	 */
	*regoff = addr_offset1;

	return 0;
}

/**
 * get_eff_addr_sib() - Obtain referenced effective address via SIB
 * @insn:	Instruction. Must be valid.
 * @regs:	Register values as seen when entering kernel mode
 * @regoff:	Obtained operand offset, in pt_regs, associated with segment
 * @eff_addr:	Obtained effective address
 *
 * Obtain the effective address referenced by the SIB byte of @insn. After
 * identifying the registers involved in the indexed, register-indirect memory
 * reference, its value is obtained from the operands in @regs. The computed
 * address is stored @eff_addr. Also, the register operand that indicates the
 * associated segment is stored in @regoff, this parameter can later be used to
 * determine such segment.
 *
 * Returns:
 *
 * 0 on success. @eff_addr will have the referenced effective address.
 * @base_offset will have a register, as an offset from the base of pt_regs,
 * that can be used to resolve the associated segment.
 *
 * -EINVAL on error.
 */
static int get_eff_addr_sib(struct insn *insn, struct pt_regs *regs,
			    int *base_offset, long *eff_addr)
{
	long base, indx;
	int indx_offset;

	if (insn->addr_bytes != 8 && insn->addr_bytes != 4)
		return -EINVAL;

	insn_get_modrm(insn);

	if (!insn->modrm.nbytes)
		return -EINVAL;

	if (X86_MODRM_MOD(insn->modrm.value) > 2)
		return -EINVAL;

	insn_get_sib(insn);

	if (!insn->sib.nbytes)
		return -EINVAL;

	*base_offset = get_reg_offset(insn, regs, REG_TYPE_BASE);
	indx_offset = get_reg_offset(insn, regs, REG_TYPE_INDEX);

	/*
	 * Negative values in the base and index offset means an error when
	 * decoding the SIB byte. Except -EDOM, which means that the registers
	 * should not be used in the address computation.
	 */
	if (*base_offset == -EDOM)
		base = 0;
	else if (*base_offset < 0)
		return -EINVAL;
	else
		base = regs_get_register(regs, *base_offset);

	if (indx_offset == -EDOM)
		indx = 0;
	else if (indx_offset < 0)
		return -EINVAL;
	else
		indx = regs_get_register(regs, indx_offset);

	if (insn->addr_bytes == 4) {
		int addr32, base32, idx32;

		base32 = base & 0xffffffff;
		idx32 = indx & 0xffffffff;

		addr32 = base32 + idx32 * (1 << X86_SIB_SCALE(insn->sib.value));
		addr32 += insn->displacement.value;

		*eff_addr = addr32 & 0xffffffff;
	} else {
		*eff_addr = base + indx * (1 << X86_SIB_SCALE(insn->sib.value));
		*eff_addr += insn->displacement.value;
	}

	return 0;
}

/**
 * get_addr_ref_16() - Obtain the 16-bit address referred by instruction
 * @insn:	Instruction containing ModRM byte and displacement
 * @regs:	Register values as seen when entering kernel mode
 *
 * This function is to be used with 16-bit address encodings. Obtain the memory
 * address referred by the instruction's ModRM and displacement bytes. Also, the
 * segment used as base is determined by either any segment override prefixes in
 * @insn or the default segment of the registers involved in the address
 * computation. In protected mode, segment limits are enforced.
 *
 * Returns:
 *
 * Linear address referenced by the instruction operands on success.
 *
 * -1L on error.
 */
static void __user *get_addr_ref_16(struct insn *insn, struct pt_regs *regs)
{
	unsigned long linear_addr = -1L, seg_base, seg_limit;
	int ret, regoff;
	short eff_addr;
	long tmp;

	insn_get_modrm(insn);
	insn_get_displacement(insn);

	if (insn->addr_bytes != 2)
		goto out;

	if (X86_MODRM_MOD(insn->modrm.value) == 3) {
		ret = get_eff_addr_reg(insn, regs, &regoff, &tmp);
		if (ret)
			goto out;

		eff_addr = tmp;
	} else {
		ret = get_eff_addr_modrm_16(insn, regs, &regoff, &eff_addr);
		if (ret)
			goto out;
	}

	ret = get_seg_base_limit(insn, regs, regoff, &seg_base, &seg_limit);
	if (ret)
		goto out;

	/*
	 * Before computing the linear address, make sure the effective address
	 * is within the limits of the segment. In virtual-8086 mode, segment
	 * limits are not enforced. In such a case, the segment limit is -1L to
	 * reflect this fact.
	 */
	if ((unsigned long)(eff_addr & 0xffff) > seg_limit)
		goto out;

	linear_addr = (unsigned long)(eff_addr & 0xffff) + seg_base;

	/* Limit linear address to 20 bits */
	if (v8086_mode(regs))
		linear_addr &= 0xfffff;

out:
	return (void __user *)linear_addr;
}

/**
 * get_addr_ref_32() - Obtain a 32-bit linear address
 * @insn:	Instruction with ModRM, SIB bytes and displacement
 * @regs:	Register values as seen when entering kernel mode
 *
 * This function is to be used with 32-bit address encodings to obtain the
 * linear memory address referred by the instruction's ModRM, SIB,
 * displacement bytes and segment base address, as applicable. If in protected
 * mode, segment limits are enforced.
 *
 * Returns:
 *
 * Linear address referenced by instruction and registers on success.
 *
 * -1L on error.
 */
static void __user *get_addr_ref_32(struct insn *insn, struct pt_regs *regs)
{
	unsigned long linear_addr = -1L, seg_base, seg_limit;
	int eff_addr, regoff;
	long tmp;
	int ret;

	if (insn->addr_bytes != 4)
		goto out;

	if (X86_MODRM_MOD(insn->modrm.value) == 3) {
		ret = get_eff_addr_reg(insn, regs, &regoff, &tmp);
		if (ret)
			goto out;

		eff_addr = tmp;

	} else {
		if (insn->sib.nbytes) {
			ret = get_eff_addr_sib(insn, regs, &regoff, &tmp);
			if (ret)
				goto out;

			eff_addr = tmp;
		} else {
			ret = get_eff_addr_modrm(insn, regs, &regoff, &tmp);
			if (ret)
				goto out;

			eff_addr = tmp;
		}
	}

	ret = get_seg_base_limit(insn, regs, regoff, &seg_base, &seg_limit);
	if (ret)
		goto out;

	/*
	 * In protected mode, before computing the linear address, make sure
	 * the effective address is within the limits of the segment.
	 * 32-bit addresses can be used in long and virtual-8086 modes if an
	 * address override prefix is used. In such cases, segment limits are
	 * not enforced. When in virtual-8086 mode, the segment limit is -1L
	 * to reflect this situation.
	 *
	 * After computed, the effective address is treated as an unsigned
	 * quantity.
	 */
	if (!any_64bit_mode(regs) && ((unsigned int)eff_addr > seg_limit))
		goto out;

	/*
	 * Even though 32-bit address encodings are allowed in virtual-8086
	 * mode, the address range is still limited to [0x-0xffff].
	 */
	if (v8086_mode(regs) && (eff_addr & ~0xffff))
		goto out;

	/*
	 * Data type long could be 64 bits in size. Ensure that our 32-bit
	 * effective address is not sign-extended when computing the linear
	 * address.
	 */
	linear_addr = (unsigned long)(eff_addr & 0xffffffff) + seg_base;

	/* Limit linear address to 20 bits */
	if (v8086_mode(regs))
		linear_addr &= 0xfffff;

out:
	return (void __user *)linear_addr;
}

/**
 * get_addr_ref_64() - Obtain a 64-bit linear address
 * @insn:	Instruction struct with ModRM and SIB bytes and displacement
 * @regs:	Structure with register values as seen when entering kernel mode
 *
 * This function is to be used with 64-bit address encodings to obtain the
 * linear memory address referred by the instruction's ModRM, SIB,
 * displacement bytes and segment base address, as applicable.
 *
 * Returns:
 *
 * Linear address referenced by instruction and registers on success.
 *
 * -1L on error.
 */
#ifndef CONFIG_X86_64
static void __user *get_addr_ref_64(struct insn *insn, struct pt_regs *regs)
{
	return (void __user *)-1L;
}
#else
static void __user *get_addr_ref_64(struct insn *insn, struct pt_regs *regs)
{
	unsigned long linear_addr = -1L, seg_base;
	int regoff, ret;
	long eff_addr;

	if (insn->addr_bytes != 8)
		goto out;

	if (X86_MODRM_MOD(insn->modrm.value) == 3) {
		ret = get_eff_addr_reg(insn, regs, &regoff, &eff_addr);
		if (ret)
			goto out;

	} else {
		if (insn->sib.nbytes) {
			ret = get_eff_addr_sib(insn, regs, &regoff, &eff_addr);
			if (ret)
				goto out;
		} else {
			ret = get_eff_addr_modrm(insn, regs, &regoff, &eff_addr);
			if (ret)
				goto out;
		}

	}

	ret = get_seg_base_limit(insn, regs, regoff, &seg_base, NULL);
	if (ret)
		goto out;

	linear_addr = (unsigned long)eff_addr + seg_base;

out:
	return (void __user *)linear_addr;
}
#endif /* CONFIG_X86_64 */

/**
 * insn_get_addr_ref() - Obtain the linear address referred by instruction
 * @insn:	Instruction structure containing ModRM byte and displacement
 * @regs:	Structure with register values as seen when entering kernel mode
 *
 * Obtain the linear address referred by the instruction's ModRM, SIB and
 * displacement bytes, and segment base, as applicable. In protected mode,
 * segment limits are enforced.
 *
 * Returns:
 *
 * Linear address referenced by instruction and registers on success.
 *
 * -1L on error.
 */
void __user *insn_get_addr_ref(struct insn *insn, struct pt_regs *regs)
{
	if (!insn || !regs)
		return (void __user *)-1L;

	switch (insn->addr_bytes) {
	case 2:
		return get_addr_ref_16(insn, regs);
	case 4:
		return get_addr_ref_32(insn, regs);
	case 8:
		return get_addr_ref_64(insn, regs);
	default:
		return (void __user *)-1L;
	}
}

/**
 * insn_fetch_from_user() - Copy instruction bytes from user-space memory
 * @regs:	Structure with register values as seen when entering kernel mode
 * @buf:	Array to store the fetched instruction
 *
 * Gets the linear address of the instruction and copies the instruction bytes
 * to the buf.
 *
 * Returns:
 *
 * Number of instruction bytes copied.
 *
 * 0 if nothing was copied.
 */
int insn_fetch_from_user(struct pt_regs *regs, unsigned char buf[MAX_INSN_SIZE])
{
	unsigned long seg_base = 0;
	int not_copied;

	/*
	 * If not in user-space long mode, a custom code segment could be in
	 * use. This is true in protected mode (if the process defined a local
	 * descriptor table), or virtual-8086 mode. In most of the cases
	 * seg_base will be zero as in USER_CS.
	 */
	if (!user_64bit_mode(regs)) {
		seg_base = insn_get_seg_base(regs, INAT_SEG_REG_CS);
		if (seg_base == -1L)
			return 0;
	}


	not_copied = copy_from_user(buf, (void __user *)(seg_base + regs->ip),
				    MAX_INSN_SIZE);

	return MAX_INSN_SIZE - not_copied;
}

/**
 * insn_decode() - Decode an instruction
 * @insn:	Structure to store decoded instruction
 * @regs:	Structure with register values as seen when entering kernel mode
 * @buf:	Buffer containing the instruction bytes
 * @buf_size:   Number of instruction bytes available in buf
 *
 * Decodes the instruction provided in buf and stores the decoding results in
 * insn. Also determines the correct address and operand sizes.
 *
 * Returns:
 *
 * True if instruction was decoded, False otherwise.
 */
bool insn_decode(struct insn *insn, struct pt_regs *regs,
		 unsigned char buf[MAX_INSN_SIZE], int buf_size)
{
	int seg_defs;

	insn_init(insn, buf, buf_size, user_64bit_mode(regs));

	/*
	 * Override the default operand and address sizes with what is specified
	 * in the code segment descriptor. The instruction decoder only sets
	 * the address size it to either 4 or 8 address bytes and does nothing
	 * for the operand bytes. This OK for most of the cases, but we could
	 * have special cases where, for instance, a 16-bit code segment
	 * descriptor is used.
	 * If there is an address override prefix, the instruction decoder
	 * correctly updates these values, even for 16-bit defaults.
	 */
	seg_defs = insn_get_code_seg_params(regs);
	if (seg_defs == -EINVAL)
		return false;

	insn->addr_bytes = INSN_CODE_SEG_ADDR_SZ(seg_defs);
	insn->opnd_bytes = INSN_CODE_SEG_OPND_SZ(seg_defs);

	insn_get_length(insn);
	if (buf_size < insn->length)
		return false;

	return true;
}
