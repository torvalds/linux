// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Huawei Ltd.
 * Author: Jiang Liu <liuj97@gmail.com>
 *
 * Copyright (C) 2014-2016 Zi Shen Lim <zlim.lnx@gmail.com>
 */
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/printk.h>
#include <linux/sizes.h>
#include <linux/types.h>

#include <asm/debug-monitors.h>
#include <asm/errno.h>
#include <asm/insn.h>
#include <asm/kprobes.h>

#define AARCH64_INSN_SF_BIT	BIT(31)
#define AARCH64_INSN_N_BIT	BIT(22)
#define AARCH64_INSN_LSL_12	BIT(22)

static int __kprobes aarch64_get_imm_shift_mask(enum aarch64_insn_imm_type type,
						u32 *maskp, int *shiftp)
{
	u32 mask;
	int shift;

	switch (type) {
	case AARCH64_INSN_IMM_26:
		mask = BIT(26) - 1;
		shift = 0;
		break;
	case AARCH64_INSN_IMM_19:
		mask = BIT(19) - 1;
		shift = 5;
		break;
	case AARCH64_INSN_IMM_16:
		mask = BIT(16) - 1;
		shift = 5;
		break;
	case AARCH64_INSN_IMM_14:
		mask = BIT(14) - 1;
		shift = 5;
		break;
	case AARCH64_INSN_IMM_12:
		mask = BIT(12) - 1;
		shift = 10;
		break;
	case AARCH64_INSN_IMM_9:
		mask = BIT(9) - 1;
		shift = 12;
		break;
	case AARCH64_INSN_IMM_7:
		mask = BIT(7) - 1;
		shift = 15;
		break;
	case AARCH64_INSN_IMM_6:
	case AARCH64_INSN_IMM_S:
		mask = BIT(6) - 1;
		shift = 10;
		break;
	case AARCH64_INSN_IMM_R:
		mask = BIT(6) - 1;
		shift = 16;
		break;
	case AARCH64_INSN_IMM_N:
		mask = 1;
		shift = 22;
		break;
	default:
		return -EINVAL;
	}

	*maskp = mask;
	*shiftp = shift;

	return 0;
}

#define ADR_IMM_HILOSPLIT	2
#define ADR_IMM_SIZE		SZ_2M
#define ADR_IMM_LOMASK		((1 << ADR_IMM_HILOSPLIT) - 1)
#define ADR_IMM_HIMASK		((ADR_IMM_SIZE >> ADR_IMM_HILOSPLIT) - 1)
#define ADR_IMM_LOSHIFT		29
#define ADR_IMM_HISHIFT		5

u64 aarch64_insn_decode_immediate(enum aarch64_insn_imm_type type, u32 insn)
{
	u32 immlo, immhi, mask;
	int shift;

	switch (type) {
	case AARCH64_INSN_IMM_ADR:
		shift = 0;
		immlo = (insn >> ADR_IMM_LOSHIFT) & ADR_IMM_LOMASK;
		immhi = (insn >> ADR_IMM_HISHIFT) & ADR_IMM_HIMASK;
		insn = (immhi << ADR_IMM_HILOSPLIT) | immlo;
		mask = ADR_IMM_SIZE - 1;
		break;
	default:
		if (aarch64_get_imm_shift_mask(type, &mask, &shift) < 0) {
			pr_err("%s: unknown immediate encoding %d\n", __func__,
			       type);
			return 0;
		}
	}

	return (insn >> shift) & mask;
}

u32 __kprobes aarch64_insn_encode_immediate(enum aarch64_insn_imm_type type,
				  u32 insn, u64 imm)
{
	u32 immlo, immhi, mask;
	int shift;

	if (insn == AARCH64_BREAK_FAULT)
		return AARCH64_BREAK_FAULT;

	switch (type) {
	case AARCH64_INSN_IMM_ADR:
		shift = 0;
		immlo = (imm & ADR_IMM_LOMASK) << ADR_IMM_LOSHIFT;
		imm >>= ADR_IMM_HILOSPLIT;
		immhi = (imm & ADR_IMM_HIMASK) << ADR_IMM_HISHIFT;
		imm = immlo | immhi;
		mask = ((ADR_IMM_LOMASK << ADR_IMM_LOSHIFT) |
			(ADR_IMM_HIMASK << ADR_IMM_HISHIFT));
		break;
	default:
		if (aarch64_get_imm_shift_mask(type, &mask, &shift) < 0) {
			pr_err("%s: unknown immediate encoding %d\n", __func__,
			       type);
			return AARCH64_BREAK_FAULT;
		}
	}

	/* Update the immediate field. */
	insn &= ~(mask << shift);
	insn |= (imm & mask) << shift;

	return insn;
}

u32 aarch64_insn_decode_register(enum aarch64_insn_register_type type,
					u32 insn)
{
	int shift;

	switch (type) {
	case AARCH64_INSN_REGTYPE_RT:
	case AARCH64_INSN_REGTYPE_RD:
		shift = 0;
		break;
	case AARCH64_INSN_REGTYPE_RN:
		shift = 5;
		break;
	case AARCH64_INSN_REGTYPE_RT2:
	case AARCH64_INSN_REGTYPE_RA:
		shift = 10;
		break;
	case AARCH64_INSN_REGTYPE_RM:
		shift = 16;
		break;
	default:
		pr_err("%s: unknown register type encoding %d\n", __func__,
		       type);
		return 0;
	}

	return (insn >> shift) & GENMASK(4, 0);
}

static u32 aarch64_insn_encode_register(enum aarch64_insn_register_type type,
					u32 insn,
					enum aarch64_insn_register reg)
{
	int shift;

	if (insn == AARCH64_BREAK_FAULT)
		return AARCH64_BREAK_FAULT;

	if (reg < AARCH64_INSN_REG_0 || reg > AARCH64_INSN_REG_SP) {
		pr_err("%s: unknown register encoding %d\n", __func__, reg);
		return AARCH64_BREAK_FAULT;
	}

	switch (type) {
	case AARCH64_INSN_REGTYPE_RT:
	case AARCH64_INSN_REGTYPE_RD:
		shift = 0;
		break;
	case AARCH64_INSN_REGTYPE_RN:
		shift = 5;
		break;
	case AARCH64_INSN_REGTYPE_RT2:
	case AARCH64_INSN_REGTYPE_RA:
		shift = 10;
		break;
	case AARCH64_INSN_REGTYPE_RM:
	case AARCH64_INSN_REGTYPE_RS:
		shift = 16;
		break;
	default:
		pr_err("%s: unknown register type encoding %d\n", __func__,
		       type);
		return AARCH64_BREAK_FAULT;
	}

	insn &= ~(GENMASK(4, 0) << shift);
	insn |= reg << shift;

	return insn;
}

static const u32 aarch64_insn_ldst_size[] = {
	[AARCH64_INSN_SIZE_8] = 0,
	[AARCH64_INSN_SIZE_16] = 1,
	[AARCH64_INSN_SIZE_32] = 2,
	[AARCH64_INSN_SIZE_64] = 3,
};

static u32 aarch64_insn_encode_ldst_size(enum aarch64_insn_size_type type,
					 u32 insn)
{
	u32 size;

	if (type < AARCH64_INSN_SIZE_8 || type > AARCH64_INSN_SIZE_64) {
		pr_err("%s: unknown size encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	size = aarch64_insn_ldst_size[type];
	insn &= ~GENMASK(31, 30);
	insn |= size << 30;

	return insn;
}

static inline long label_imm_common(unsigned long pc, unsigned long addr,
				     long range)
{
	long offset;

	if ((pc & 0x3) || (addr & 0x3)) {
		pr_err("%s: A64 instructions must be word aligned\n", __func__);
		return range;
	}

	offset = ((long)addr - (long)pc);

	if (offset < -range || offset >= range) {
		pr_err("%s: offset out of range\n", __func__);
		return range;
	}

	return offset;
}

u32 __kprobes aarch64_insn_gen_branch_imm(unsigned long pc, unsigned long addr,
					  enum aarch64_insn_branch_type type)
{
	u32 insn;
	long offset;

	/*
	 * B/BL support [-128M, 128M) offset
	 * ARM64 virtual address arrangement guarantees all kernel and module
	 * texts are within +/-128M.
	 */
	offset = label_imm_common(pc, addr, SZ_128M);
	if (offset >= SZ_128M)
		return AARCH64_BREAK_FAULT;

	switch (type) {
	case AARCH64_INSN_BRANCH_LINK:
		insn = aarch64_insn_get_bl_value();
		break;
	case AARCH64_INSN_BRANCH_NOLINK:
		insn = aarch64_insn_get_b_value();
		break;
	default:
		pr_err("%s: unknown branch encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_26, insn,
					     offset >> 2);
}

u32 aarch64_insn_gen_comp_branch_imm(unsigned long pc, unsigned long addr,
				     enum aarch64_insn_register reg,
				     enum aarch64_insn_variant variant,
				     enum aarch64_insn_branch_type type)
{
	u32 insn;
	long offset;

	offset = label_imm_common(pc, addr, SZ_1M);
	if (offset >= SZ_1M)
		return AARCH64_BREAK_FAULT;

	switch (type) {
	case AARCH64_INSN_BRANCH_COMP_ZERO:
		insn = aarch64_insn_get_cbz_value();
		break;
	case AARCH64_INSN_BRANCH_COMP_NONZERO:
		insn = aarch64_insn_get_cbnz_value();
		break;
	default:
		pr_err("%s: unknown branch encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT, insn, reg);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_19, insn,
					     offset >> 2);
}

u32 aarch64_insn_gen_cond_branch_imm(unsigned long pc, unsigned long addr,
				     enum aarch64_insn_condition cond)
{
	u32 insn;
	long offset;

	offset = label_imm_common(pc, addr, SZ_1M);

	insn = aarch64_insn_get_bcond_value();

	if (cond < AARCH64_INSN_COND_EQ || cond > AARCH64_INSN_COND_AL) {
		pr_err("%s: unknown condition encoding %d\n", __func__, cond);
		return AARCH64_BREAK_FAULT;
	}
	insn |= cond;

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_19, insn,
					     offset >> 2);
}

u32 aarch64_insn_gen_branch_reg(enum aarch64_insn_register reg,
				enum aarch64_insn_branch_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_BRANCH_NOLINK:
		insn = aarch64_insn_get_br_value();
		break;
	case AARCH64_INSN_BRANCH_LINK:
		insn = aarch64_insn_get_blr_value();
		break;
	case AARCH64_INSN_BRANCH_RETURN:
		insn = aarch64_insn_get_ret_value();
		break;
	default:
		pr_err("%s: unknown branch encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, reg);
}

u32 aarch64_insn_gen_load_store_reg(enum aarch64_insn_register reg,
				    enum aarch64_insn_register base,
				    enum aarch64_insn_register offset,
				    enum aarch64_insn_size_type size,
				    enum aarch64_insn_ldst_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_LDST_LOAD_REG_OFFSET:
		insn = aarch64_insn_get_ldr_reg_value();
		break;
	case AARCH64_INSN_LDST_SIGNED_LOAD_REG_OFFSET:
		insn = aarch64_insn_get_signed_ldr_reg_value();
		break;
	case AARCH64_INSN_LDST_STORE_REG_OFFSET:
		insn = aarch64_insn_get_str_reg_value();
		break;
	default:
		pr_err("%s: unknown load/store encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_ldst_size(size, insn);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT, insn, reg);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn,
					    base);

	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RM, insn,
					    offset);
}

u32 aarch64_insn_gen_load_store_imm(enum aarch64_insn_register reg,
				    enum aarch64_insn_register base,
				    unsigned int imm,
				    enum aarch64_insn_size_type size,
				    enum aarch64_insn_ldst_type type)
{
	u32 insn;
	u32 shift;

	if (size < AARCH64_INSN_SIZE_8 || size > AARCH64_INSN_SIZE_64) {
		pr_err("%s: unknown size encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	shift = aarch64_insn_ldst_size[size];
	if (imm & ~(BIT(12 + shift) - BIT(shift))) {
		pr_err("%s: invalid imm: %d\n", __func__, imm);
		return AARCH64_BREAK_FAULT;
	}

	imm >>= shift;

	switch (type) {
	case AARCH64_INSN_LDST_LOAD_IMM_OFFSET:
		insn = aarch64_insn_get_ldr_imm_value();
		break;
	case AARCH64_INSN_LDST_SIGNED_LOAD_IMM_OFFSET:
		insn = aarch64_insn_get_signed_load_imm_value();
		break;
	case AARCH64_INSN_LDST_STORE_IMM_OFFSET:
		insn = aarch64_insn_get_str_imm_value();
		break;
	default:
		pr_err("%s: unknown load/store encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_ldst_size(size, insn);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT, insn, reg);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn,
					    base);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_12, insn, imm);
}

u32 aarch64_insn_gen_load_literal(unsigned long pc, unsigned long addr,
				  enum aarch64_insn_register reg,
				  bool is64bit)
{
	u32 insn;
	long offset;

	offset = label_imm_common(pc, addr, SZ_1M);
	if (offset >= SZ_1M)
		return AARCH64_BREAK_FAULT;

	insn = aarch64_insn_get_ldr_lit_value();

	if (is64bit)
		insn |= BIT(30);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT, insn, reg);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_19, insn,
					     offset >> 2);
}

u32 aarch64_insn_gen_load_store_pair(enum aarch64_insn_register reg1,
				     enum aarch64_insn_register reg2,
				     enum aarch64_insn_register base,
				     int offset,
				     enum aarch64_insn_variant variant,
				     enum aarch64_insn_ldst_type type)
{
	u32 insn;
	int shift;

	switch (type) {
	case AARCH64_INSN_LDST_LOAD_PAIR_PRE_INDEX:
		insn = aarch64_insn_get_ldp_pre_value();
		break;
	case AARCH64_INSN_LDST_STORE_PAIR_PRE_INDEX:
		insn = aarch64_insn_get_stp_pre_value();
		break;
	case AARCH64_INSN_LDST_LOAD_PAIR_POST_INDEX:
		insn = aarch64_insn_get_ldp_post_value();
		break;
	case AARCH64_INSN_LDST_STORE_PAIR_POST_INDEX:
		insn = aarch64_insn_get_stp_post_value();
		break;
	default:
		pr_err("%s: unknown load/store encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		if ((offset & 0x3) || (offset < -256) || (offset > 252)) {
			pr_err("%s: offset must be multiples of 4 in the range of [-256, 252] %d\n",
			       __func__, offset);
			return AARCH64_BREAK_FAULT;
		}
		shift = 2;
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		if ((offset & 0x7) || (offset < -512) || (offset > 504)) {
			pr_err("%s: offset must be multiples of 8 in the range of [-512, 504] %d\n",
			       __func__, offset);
			return AARCH64_BREAK_FAULT;
		}
		shift = 3;
		insn |= AARCH64_INSN_SF_BIT;
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT, insn,
					    reg1);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT2, insn,
					    reg2);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn,
					    base);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_7, insn,
					     offset >> shift);
}

u32 aarch64_insn_gen_load_store_ex(enum aarch64_insn_register reg,
				   enum aarch64_insn_register base,
				   enum aarch64_insn_register state,
				   enum aarch64_insn_size_type size,
				   enum aarch64_insn_ldst_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_LDST_LOAD_EX:
	case AARCH64_INSN_LDST_LOAD_ACQ_EX:
		insn = aarch64_insn_get_load_ex_value();
		if (type == AARCH64_INSN_LDST_LOAD_ACQ_EX)
			insn |= BIT(15);
		break;
	case AARCH64_INSN_LDST_STORE_EX:
	case AARCH64_INSN_LDST_STORE_REL_EX:
		insn = aarch64_insn_get_store_ex_value();
		if (type == AARCH64_INSN_LDST_STORE_REL_EX)
			insn |= BIT(15);
		break;
	default:
		pr_err("%s: unknown load/store exclusive encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_ldst_size(size, insn);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT, insn,
					    reg);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn,
					    base);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT2, insn,
					    AARCH64_INSN_REG_ZR);

	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RS, insn,
					    state);
}

#ifdef CONFIG_ARM64_LSE_ATOMICS
static u32 aarch64_insn_encode_ldst_order(enum aarch64_insn_mem_order_type type,
					  u32 insn)
{
	u32 order;

	switch (type) {
	case AARCH64_INSN_MEM_ORDER_NONE:
		order = 0;
		break;
	case AARCH64_INSN_MEM_ORDER_ACQ:
		order = 2;
		break;
	case AARCH64_INSN_MEM_ORDER_REL:
		order = 1;
		break;
	case AARCH64_INSN_MEM_ORDER_ACQREL:
		order = 3;
		break;
	default:
		pr_err("%s: unknown mem order %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	insn &= ~GENMASK(23, 22);
	insn |= order << 22;

	return insn;
}

u32 aarch64_insn_gen_atomic_ld_op(enum aarch64_insn_register result,
				  enum aarch64_insn_register address,
				  enum aarch64_insn_register value,
				  enum aarch64_insn_size_type size,
				  enum aarch64_insn_mem_atomic_op op,
				  enum aarch64_insn_mem_order_type order)
{
	u32 insn;

	switch (op) {
	case AARCH64_INSN_MEM_ATOMIC_ADD:
		insn = aarch64_insn_get_ldadd_value();
		break;
	case AARCH64_INSN_MEM_ATOMIC_CLR:
		insn = aarch64_insn_get_ldclr_value();
		break;
	case AARCH64_INSN_MEM_ATOMIC_EOR:
		insn = aarch64_insn_get_ldeor_value();
		break;
	case AARCH64_INSN_MEM_ATOMIC_SET:
		insn = aarch64_insn_get_ldset_value();
		break;
	case AARCH64_INSN_MEM_ATOMIC_SWP:
		insn = aarch64_insn_get_swp_value();
		break;
	default:
		pr_err("%s: unimplemented mem atomic op %d\n", __func__, op);
		return AARCH64_BREAK_FAULT;
	}

	switch (size) {
	case AARCH64_INSN_SIZE_32:
	case AARCH64_INSN_SIZE_64:
		break;
	default:
		pr_err("%s: unimplemented size encoding %d\n", __func__, size);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_ldst_size(size, insn);

	insn = aarch64_insn_encode_ldst_order(order, insn);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT, insn,
					    result);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn,
					    address);

	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RS, insn,
					    value);
}

static u32 aarch64_insn_encode_cas_order(enum aarch64_insn_mem_order_type type,
					 u32 insn)
{
	u32 order;

	switch (type) {
	case AARCH64_INSN_MEM_ORDER_NONE:
		order = 0;
		break;
	case AARCH64_INSN_MEM_ORDER_ACQ:
		order = BIT(22);
		break;
	case AARCH64_INSN_MEM_ORDER_REL:
		order = BIT(15);
		break;
	case AARCH64_INSN_MEM_ORDER_ACQREL:
		order = BIT(15) | BIT(22);
		break;
	default:
		pr_err("%s: unknown mem order %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	insn &= ~(BIT(15) | BIT(22));
	insn |= order;

	return insn;
}

u32 aarch64_insn_gen_cas(enum aarch64_insn_register result,
			 enum aarch64_insn_register address,
			 enum aarch64_insn_register value,
			 enum aarch64_insn_size_type size,
			 enum aarch64_insn_mem_order_type order)
{
	u32 insn;

	switch (size) {
	case AARCH64_INSN_SIZE_32:
	case AARCH64_INSN_SIZE_64:
		break;
	default:
		pr_err("%s: unimplemented size encoding %d\n", __func__, size);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_get_cas_value();

	insn = aarch64_insn_encode_ldst_size(size, insn);

	insn = aarch64_insn_encode_cas_order(order, insn);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT, insn,
					    result);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn,
					    address);

	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RS, insn,
					    value);
}
#endif

u32 aarch64_insn_gen_add_sub_imm(enum aarch64_insn_register dst,
				 enum aarch64_insn_register src,
				 int imm, enum aarch64_insn_variant variant,
				 enum aarch64_insn_adsb_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_ADSB_ADD:
		insn = aarch64_insn_get_add_imm_value();
		break;
	case AARCH64_INSN_ADSB_SUB:
		insn = aarch64_insn_get_sub_imm_value();
		break;
	case AARCH64_INSN_ADSB_ADD_SETFLAGS:
		insn = aarch64_insn_get_adds_imm_value();
		break;
	case AARCH64_INSN_ADSB_SUB_SETFLAGS:
		insn = aarch64_insn_get_subs_imm_value();
		break;
	default:
		pr_err("%s: unknown add/sub encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	/* We can't encode more than a 24bit value (12bit + 12bit shift) */
	if (imm & ~(BIT(24) - 1))
		goto out;

	/* If we have something in the top 12 bits... */
	if (imm & ~(SZ_4K - 1)) {
		/* ... and in the low 12 bits -> error */
		if (imm & (SZ_4K - 1))
			goto out;

		imm >>= 12;
		insn |= AARCH64_INSN_LSL_12;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, src);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_12, insn, imm);

out:
	pr_err("%s: invalid immediate encoding %d\n", __func__, imm);
	return AARCH64_BREAK_FAULT;
}

u32 aarch64_insn_gen_bitfield(enum aarch64_insn_register dst,
			      enum aarch64_insn_register src,
			      int immr, int imms,
			      enum aarch64_insn_variant variant,
			      enum aarch64_insn_bitfield_type type)
{
	u32 insn;
	u32 mask;

	switch (type) {
	case AARCH64_INSN_BITFIELD_MOVE:
		insn = aarch64_insn_get_bfm_value();
		break;
	case AARCH64_INSN_BITFIELD_MOVE_UNSIGNED:
		insn = aarch64_insn_get_ubfm_value();
		break;
	case AARCH64_INSN_BITFIELD_MOVE_SIGNED:
		insn = aarch64_insn_get_sbfm_value();
		break;
	default:
		pr_err("%s: unknown bitfield encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		mask = GENMASK(4, 0);
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT | AARCH64_INSN_N_BIT;
		mask = GENMASK(5, 0);
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	if (immr & ~mask) {
		pr_err("%s: invalid immr encoding %d\n", __func__, immr);
		return AARCH64_BREAK_FAULT;
	}
	if (imms & ~mask) {
		pr_err("%s: invalid imms encoding %d\n", __func__, imms);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, src);

	insn = aarch64_insn_encode_immediate(AARCH64_INSN_IMM_R, insn, immr);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_S, insn, imms);
}

u32 aarch64_insn_gen_movewide(enum aarch64_insn_register dst,
			      int imm, int shift,
			      enum aarch64_insn_variant variant,
			      enum aarch64_insn_movewide_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_MOVEWIDE_ZERO:
		insn = aarch64_insn_get_movz_value();
		break;
	case AARCH64_INSN_MOVEWIDE_KEEP:
		insn = aarch64_insn_get_movk_value();
		break;
	case AARCH64_INSN_MOVEWIDE_INVERSE:
		insn = aarch64_insn_get_movn_value();
		break;
	default:
		pr_err("%s: unknown movewide encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	if (imm & ~(SZ_64K - 1)) {
		pr_err("%s: invalid immediate encoding %d\n", __func__, imm);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		if (shift != 0 && shift != 16) {
			pr_err("%s: invalid shift encoding %d\n", __func__,
			       shift);
			return AARCH64_BREAK_FAULT;
		}
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		if (shift != 0 && shift != 16 && shift != 32 && shift != 48) {
			pr_err("%s: invalid shift encoding %d\n", __func__,
			       shift);
			return AARCH64_BREAK_FAULT;
		}
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	insn |= (shift >> 4) << 21;

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_16, insn, imm);
}

u32 aarch64_insn_gen_add_sub_shifted_reg(enum aarch64_insn_register dst,
					 enum aarch64_insn_register src,
					 enum aarch64_insn_register reg,
					 int shift,
					 enum aarch64_insn_variant variant,
					 enum aarch64_insn_adsb_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_ADSB_ADD:
		insn = aarch64_insn_get_add_value();
		break;
	case AARCH64_INSN_ADSB_SUB:
		insn = aarch64_insn_get_sub_value();
		break;
	case AARCH64_INSN_ADSB_ADD_SETFLAGS:
		insn = aarch64_insn_get_adds_value();
		break;
	case AARCH64_INSN_ADSB_SUB_SETFLAGS:
		insn = aarch64_insn_get_subs_value();
		break;
	default:
		pr_err("%s: unknown add/sub encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		if (shift & ~(SZ_32 - 1)) {
			pr_err("%s: invalid shift encoding %d\n", __func__,
			       shift);
			return AARCH64_BREAK_FAULT;
		}
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		if (shift & ~(SZ_64 - 1)) {
			pr_err("%s: invalid shift encoding %d\n", __func__,
			       shift);
			return AARCH64_BREAK_FAULT;
		}
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}


	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, src);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RM, insn, reg);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_6, insn, shift);
}

u32 aarch64_insn_gen_data1(enum aarch64_insn_register dst,
			   enum aarch64_insn_register src,
			   enum aarch64_insn_variant variant,
			   enum aarch64_insn_data1_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_DATA1_REVERSE_16:
		insn = aarch64_insn_get_rev16_value();
		break;
	case AARCH64_INSN_DATA1_REVERSE_32:
		insn = aarch64_insn_get_rev32_value();
		break;
	case AARCH64_INSN_DATA1_REVERSE_64:
		if (variant != AARCH64_INSN_VARIANT_64BIT) {
			pr_err("%s: invalid variant for reverse64 %d\n",
			       __func__, variant);
			return AARCH64_BREAK_FAULT;
		}
		insn = aarch64_insn_get_rev64_value();
		break;
	default:
		pr_err("%s: unknown data1 encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, src);
}

u32 aarch64_insn_gen_data2(enum aarch64_insn_register dst,
			   enum aarch64_insn_register src,
			   enum aarch64_insn_register reg,
			   enum aarch64_insn_variant variant,
			   enum aarch64_insn_data2_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_DATA2_UDIV:
		insn = aarch64_insn_get_udiv_value();
		break;
	case AARCH64_INSN_DATA2_SDIV:
		insn = aarch64_insn_get_sdiv_value();
		break;
	case AARCH64_INSN_DATA2_LSLV:
		insn = aarch64_insn_get_lslv_value();
		break;
	case AARCH64_INSN_DATA2_LSRV:
		insn = aarch64_insn_get_lsrv_value();
		break;
	case AARCH64_INSN_DATA2_ASRV:
		insn = aarch64_insn_get_asrv_value();
		break;
	case AARCH64_INSN_DATA2_RORV:
		insn = aarch64_insn_get_rorv_value();
		break;
	default:
		pr_err("%s: unknown data2 encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, src);

	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RM, insn, reg);
}

u32 aarch64_insn_gen_data3(enum aarch64_insn_register dst,
			   enum aarch64_insn_register src,
			   enum aarch64_insn_register reg1,
			   enum aarch64_insn_register reg2,
			   enum aarch64_insn_variant variant,
			   enum aarch64_insn_data3_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_DATA3_MADD:
		insn = aarch64_insn_get_madd_value();
		break;
	case AARCH64_INSN_DATA3_MSUB:
		insn = aarch64_insn_get_msub_value();
		break;
	default:
		pr_err("%s: unknown data3 encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RA, insn, src);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn,
					    reg1);

	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RM, insn,
					    reg2);
}

u32 aarch64_insn_gen_logical_shifted_reg(enum aarch64_insn_register dst,
					 enum aarch64_insn_register src,
					 enum aarch64_insn_register reg,
					 int shift,
					 enum aarch64_insn_variant variant,
					 enum aarch64_insn_logic_type type)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_LOGIC_AND:
		insn = aarch64_insn_get_and_value();
		break;
	case AARCH64_INSN_LOGIC_BIC:
		insn = aarch64_insn_get_bic_value();
		break;
	case AARCH64_INSN_LOGIC_ORR:
		insn = aarch64_insn_get_orr_value();
		break;
	case AARCH64_INSN_LOGIC_ORN:
		insn = aarch64_insn_get_orn_value();
		break;
	case AARCH64_INSN_LOGIC_EOR:
		insn = aarch64_insn_get_eor_value();
		break;
	case AARCH64_INSN_LOGIC_EON:
		insn = aarch64_insn_get_eon_value();
		break;
	case AARCH64_INSN_LOGIC_AND_SETFLAGS:
		insn = aarch64_insn_get_ands_value();
		break;
	case AARCH64_INSN_LOGIC_BIC_SETFLAGS:
		insn = aarch64_insn_get_bics_value();
		break;
	default:
		pr_err("%s: unknown logical encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		if (shift & ~(SZ_32 - 1)) {
			pr_err("%s: invalid shift encoding %d\n", __func__,
			       shift);
			return AARCH64_BREAK_FAULT;
		}
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		if (shift & ~(SZ_64 - 1)) {
			pr_err("%s: invalid shift encoding %d\n", __func__,
			       shift);
			return AARCH64_BREAK_FAULT;
		}
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}


	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, dst);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, src);

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RM, insn, reg);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_6, insn, shift);
}

/*
 * MOV (register) is architecturally an alias of ORR (shifted register) where
 * MOV <*d>, <*m> is equivalent to ORR <*d>, <*ZR>, <*m>
 */
u32 aarch64_insn_gen_move_reg(enum aarch64_insn_register dst,
			      enum aarch64_insn_register src,
			      enum aarch64_insn_variant variant)
{
	return aarch64_insn_gen_logical_shifted_reg(dst, AARCH64_INSN_REG_ZR,
						    src, 0, variant,
						    AARCH64_INSN_LOGIC_ORR);
}

u32 aarch64_insn_gen_adr(unsigned long pc, unsigned long addr,
			 enum aarch64_insn_register reg,
			 enum aarch64_insn_adr_type type)
{
	u32 insn;
	s32 offset;

	switch (type) {
	case AARCH64_INSN_ADR_TYPE_ADR:
		insn = aarch64_insn_get_adr_value();
		offset = addr - pc;
		break;
	case AARCH64_INSN_ADR_TYPE_ADRP:
		insn = aarch64_insn_get_adrp_value();
		offset = (addr - ALIGN_DOWN(pc, SZ_4K)) >> 12;
		break;
	default:
		pr_err("%s: unknown adr encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	if (offset < -SZ_1M || offset >= SZ_1M)
		return AARCH64_BREAK_FAULT;

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, reg);

	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_ADR, insn, offset);
}

/*
 * Decode the imm field of a branch, and return the byte offset as a
 * signed value (so it can be used when computing a new branch
 * target).
 */
s32 aarch64_get_branch_offset(u32 insn)
{
	s32 imm;

	if (aarch64_insn_is_b(insn) || aarch64_insn_is_bl(insn)) {
		imm = aarch64_insn_decode_immediate(AARCH64_INSN_IMM_26, insn);
		return (imm << 6) >> 4;
	}

	if (aarch64_insn_is_cbz(insn) || aarch64_insn_is_cbnz(insn) ||
	    aarch64_insn_is_bcond(insn)) {
		imm = aarch64_insn_decode_immediate(AARCH64_INSN_IMM_19, insn);
		return (imm << 13) >> 11;
	}

	if (aarch64_insn_is_tbz(insn) || aarch64_insn_is_tbnz(insn)) {
		imm = aarch64_insn_decode_immediate(AARCH64_INSN_IMM_14, insn);
		return (imm << 18) >> 16;
	}

	/* Unhandled instruction */
	BUG();
}

/*
 * Encode the displacement of a branch in the imm field and return the
 * updated instruction.
 */
u32 aarch64_set_branch_offset(u32 insn, s32 offset)
{
	if (aarch64_insn_is_b(insn) || aarch64_insn_is_bl(insn))
		return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_26, insn,
						     offset >> 2);

	if (aarch64_insn_is_cbz(insn) || aarch64_insn_is_cbnz(insn) ||
	    aarch64_insn_is_bcond(insn))
		return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_19, insn,
						     offset >> 2);

	if (aarch64_insn_is_tbz(insn) || aarch64_insn_is_tbnz(insn))
		return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_14, insn,
						     offset >> 2);

	/* Unhandled instruction */
	BUG();
}

s32 aarch64_insn_adrp_get_offset(u32 insn)
{
	BUG_ON(!aarch64_insn_is_adrp(insn));
	return aarch64_insn_decode_immediate(AARCH64_INSN_IMM_ADR, insn) << 12;
}

u32 aarch64_insn_adrp_set_offset(u32 insn, s32 offset)
{
	BUG_ON(!aarch64_insn_is_adrp(insn));
	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_ADR, insn,
						offset >> 12);
}

/*
 * Extract the Op/CR data from a msr/mrs instruction.
 */
u32 aarch64_insn_extract_system_reg(u32 insn)
{
	return (insn & 0x1FFFE0) >> 5;
}

bool aarch32_insn_is_wide(u32 insn)
{
	return insn >= 0xe800;
}

/*
 * Macros/defines for extracting register numbers from instruction.
 */
u32 aarch32_insn_extract_reg_num(u32 insn, int offset)
{
	return (insn & (0xf << offset)) >> offset;
}

#define OPC2_MASK	0x7
#define OPC2_OFFSET	5
u32 aarch32_insn_mcr_extract_opc2(u32 insn)
{
	return (insn & (OPC2_MASK << OPC2_OFFSET)) >> OPC2_OFFSET;
}

#define CRM_MASK	0xf
u32 aarch32_insn_mcr_extract_crm(u32 insn)
{
	return insn & CRM_MASK;
}

static bool range_of_ones(u64 val)
{
	/* Doesn't handle full ones or full zeroes */
	u64 sval = val >> __ffs64(val);

	/* One of Sean Eron Anderson's bithack tricks */
	return ((sval + 1) & (sval)) == 0;
}

static u32 aarch64_encode_immediate(u64 imm,
				    enum aarch64_insn_variant variant,
				    u32 insn)
{
	unsigned int immr, imms, n, ones, ror, esz, tmp;
	u64 mask;

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		esz = 32;
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		insn |= AARCH64_INSN_SF_BIT;
		esz = 64;
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	mask = GENMASK(esz - 1, 0);

	/* Can't encode full zeroes, full ones, or value wider than the mask */
	if (!imm || imm == mask || imm & ~mask)
		return AARCH64_BREAK_FAULT;

	/*
	 * Inverse of Replicate(). Try to spot a repeating pattern
	 * with a pow2 stride.
	 */
	for (tmp = esz / 2; tmp >= 2; tmp /= 2) {
		u64 emask = BIT(tmp) - 1;

		if ((imm & emask) != ((imm >> tmp) & emask))
			break;

		esz = tmp;
		mask = emask;
	}

	/* N is only set if we're encoding a 64bit value */
	n = esz == 64;

	/* Trim imm to the element size */
	imm &= mask;

	/* That's how many ones we need to encode */
	ones = hweight64(imm);

	/*
	 * imms is set to (ones - 1), prefixed with a string of ones
	 * and a zero if they fit. Cap it to 6 bits.
	 */
	imms  = ones - 1;
	imms |= 0xf << ffs(esz);
	imms &= BIT(6) - 1;

	/* Compute the rotation */
	if (range_of_ones(imm)) {
		/*
		 * Pattern: 0..01..10..0
		 *
		 * Compute how many rotate we need to align it right
		 */
		ror = __ffs64(imm);
	} else {
		/*
		 * Pattern: 0..01..10..01..1
		 *
		 * Fill the unused top bits with ones, and check if
		 * the result is a valid immediate (all ones with a
		 * contiguous ranges of zeroes).
		 */
		imm |= ~mask;
		if (!range_of_ones(~imm))
			return AARCH64_BREAK_FAULT;

		/*
		 * Compute the rotation to get a continuous set of
		 * ones, with the first bit set at position 0
		 */
		ror = fls64(~imm);
	}

	/*
	 * immr is the number of bits we need to rotate back to the
	 * original set of ones. Note that this is relative to the
	 * element size...
	 */
	immr = (esz - ror) % esz;

	insn = aarch64_insn_encode_immediate(AARCH64_INSN_IMM_N, insn, n);
	insn = aarch64_insn_encode_immediate(AARCH64_INSN_IMM_R, insn, immr);
	return aarch64_insn_encode_immediate(AARCH64_INSN_IMM_S, insn, imms);
}

u32 aarch64_insn_gen_logical_immediate(enum aarch64_insn_logic_type type,
				       enum aarch64_insn_variant variant,
				       enum aarch64_insn_register Rn,
				       enum aarch64_insn_register Rd,
				       u64 imm)
{
	u32 insn;

	switch (type) {
	case AARCH64_INSN_LOGIC_AND:
		insn = aarch64_insn_get_and_imm_value();
		break;
	case AARCH64_INSN_LOGIC_ORR:
		insn = aarch64_insn_get_orr_imm_value();
		break;
	case AARCH64_INSN_LOGIC_EOR:
		insn = aarch64_insn_get_eor_imm_value();
		break;
	case AARCH64_INSN_LOGIC_AND_SETFLAGS:
		insn = aarch64_insn_get_ands_imm_value();
		break;
	default:
		pr_err("%s: unknown logical encoding %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, Rd);
	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, Rn);
	return aarch64_encode_immediate(imm, variant, insn);
}

u32 aarch64_insn_gen_extr(enum aarch64_insn_variant variant,
			  enum aarch64_insn_register Rm,
			  enum aarch64_insn_register Rn,
			  enum aarch64_insn_register Rd,
			  u8 lsb)
{
	u32 insn;

	insn = aarch64_insn_get_extr_value();

	switch (variant) {
	case AARCH64_INSN_VARIANT_32BIT:
		if (lsb > 31)
			return AARCH64_BREAK_FAULT;
		break;
	case AARCH64_INSN_VARIANT_64BIT:
		if (lsb > 63)
			return AARCH64_BREAK_FAULT;
		insn |= AARCH64_INSN_SF_BIT;
		insn = aarch64_insn_encode_immediate(AARCH64_INSN_IMM_N, insn, 1);
		break;
	default:
		pr_err("%s: unknown variant encoding %d\n", __func__, variant);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_encode_immediate(AARCH64_INSN_IMM_S, insn, lsb);
	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RD, insn, Rd);
	insn = aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RN, insn, Rn);
	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RM, insn, Rm);
}

u32 aarch64_insn_gen_dmb(enum aarch64_insn_mb_type type)
{
	u32 opt;
	u32 insn;

	switch (type) {
	case AARCH64_INSN_MB_SY:
		opt = 0xf;
		break;
	case AARCH64_INSN_MB_ST:
		opt = 0xe;
		break;
	case AARCH64_INSN_MB_LD:
		opt = 0xd;
		break;
	case AARCH64_INSN_MB_ISH:
		opt = 0xb;
		break;
	case AARCH64_INSN_MB_ISHST:
		opt = 0xa;
		break;
	case AARCH64_INSN_MB_ISHLD:
		opt = 0x9;
		break;
	case AARCH64_INSN_MB_NSH:
		opt = 0x7;
		break;
	case AARCH64_INSN_MB_NSHST:
		opt = 0x6;
		break;
	case AARCH64_INSN_MB_NSHLD:
		opt = 0x5;
		break;
	default:
		pr_err("%s: unknown dmb type %d\n", __func__, type);
		return AARCH64_BREAK_FAULT;
	}

	insn = aarch64_insn_get_dmb_value();
	insn &= ~GENMASK(11, 8);
	insn |= (opt << 8);

	return insn;
}

u32 aarch64_insn_gen_mrs(enum aarch64_insn_register result,
			 enum aarch64_insn_system_register sysreg)
{
	u32 insn = aarch64_insn_get_mrs_value();

	insn &= ~GENMASK(19, 0);
	insn |= sysreg << 5;
	return aarch64_insn_encode_register(AARCH64_INSN_REGTYPE_RT,
					    insn, result);
}
