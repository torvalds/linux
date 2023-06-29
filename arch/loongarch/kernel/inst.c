// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/sizes.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/inst.h>

static DEFINE_RAW_SPINLOCK(patch_lock);

void simu_pc(struct pt_regs *regs, union loongarch_instruction insn)
{
	unsigned long pc = regs->csr_era;
	unsigned int rd = insn.reg1i20_format.rd;
	unsigned int imm = insn.reg1i20_format.immediate;

	if (pc & 3) {
		pr_warn("%s: invalid pc 0x%lx\n", __func__, pc);
		return;
	}

	switch (insn.reg1i20_format.opcode) {
	case pcaddi_op:
		regs->regs[rd] = pc + sign_extend64(imm << 2, 21);
		break;
	case pcaddu12i_op:
		regs->regs[rd] = pc + sign_extend64(imm << 12, 31);
		break;
	case pcaddu18i_op:
		regs->regs[rd] = pc + sign_extend64(imm << 18, 37);
		break;
	case pcalau12i_op:
		regs->regs[rd] = pc + sign_extend64(imm << 12, 31);
		regs->regs[rd] &= ~((1 << 12) - 1);
		break;
	default:
		pr_info("%s: unknown opcode\n", __func__);
		return;
	}

	regs->csr_era += LOONGARCH_INSN_SIZE;
}

void simu_branch(struct pt_regs *regs, union loongarch_instruction insn)
{
	unsigned int imm, imm_l, imm_h, rd, rj;
	unsigned long pc = regs->csr_era;

	if (pc & 3) {
		pr_warn("%s: invalid pc 0x%lx\n", __func__, pc);
		return;
	}

	imm_l = insn.reg0i26_format.immediate_l;
	imm_h = insn.reg0i26_format.immediate_h;
	switch (insn.reg0i26_format.opcode) {
	case b_op:
		regs->csr_era = pc + sign_extend64((imm_h << 16 | imm_l) << 2, 27);
		return;
	case bl_op:
		regs->csr_era = pc + sign_extend64((imm_h << 16 | imm_l) << 2, 27);
		regs->regs[1] = pc + LOONGARCH_INSN_SIZE;
		return;
	}

	imm_l = insn.reg1i21_format.immediate_l;
	imm_h = insn.reg1i21_format.immediate_h;
	rj = insn.reg1i21_format.rj;
	switch (insn.reg1i21_format.opcode) {
	case beqz_op:
		if (regs->regs[rj] == 0)
			regs->csr_era = pc + sign_extend64((imm_h << 16 | imm_l) << 2, 22);
		else
			regs->csr_era = pc + LOONGARCH_INSN_SIZE;
		return;
	case bnez_op:
		if (regs->regs[rj] != 0)
			regs->csr_era = pc + sign_extend64((imm_h << 16 | imm_l) << 2, 22);
		else
			regs->csr_era = pc + LOONGARCH_INSN_SIZE;
		return;
	}

	imm = insn.reg2i16_format.immediate;
	rj = insn.reg2i16_format.rj;
	rd = insn.reg2i16_format.rd;
	switch (insn.reg2i16_format.opcode) {
	case beq_op:
		if (regs->regs[rj] == regs->regs[rd])
			regs->csr_era = pc + sign_extend64(imm << 2, 17);
		else
			regs->csr_era = pc + LOONGARCH_INSN_SIZE;
		break;
	case bne_op:
		if (regs->regs[rj] != regs->regs[rd])
			regs->csr_era = pc + sign_extend64(imm << 2, 17);
		else
			regs->csr_era = pc + LOONGARCH_INSN_SIZE;
		break;
	case blt_op:
		if ((long)regs->regs[rj] < (long)regs->regs[rd])
			regs->csr_era = pc + sign_extend64(imm << 2, 17);
		else
			regs->csr_era = pc + LOONGARCH_INSN_SIZE;
		break;
	case bge_op:
		if ((long)regs->regs[rj] >= (long)regs->regs[rd])
			regs->csr_era = pc + sign_extend64(imm << 2, 17);
		else
			regs->csr_era = pc + LOONGARCH_INSN_SIZE;
		break;
	case bltu_op:
		if (regs->regs[rj] < regs->regs[rd])
			regs->csr_era = pc + sign_extend64(imm << 2, 17);
		else
			regs->csr_era = pc + LOONGARCH_INSN_SIZE;
		break;
	case bgeu_op:
		if (regs->regs[rj] >= regs->regs[rd])
			regs->csr_era = pc + sign_extend64(imm << 2, 17);
		else
			regs->csr_era = pc + LOONGARCH_INSN_SIZE;
		break;
	case jirl_op:
		regs->csr_era = regs->regs[rj] + sign_extend64(imm << 2, 17);
		regs->regs[rd] = pc + LOONGARCH_INSN_SIZE;
		break;
	default:
		pr_info("%s: unknown opcode\n", __func__);
		return;
	}
}

bool insns_not_supported(union loongarch_instruction insn)
{
	switch (insn.reg3_format.opcode) {
	case amswapw_op ... ammindbdu_op:
		pr_notice("atomic memory access instructions are not supported\n");
		return true;
	}

	switch (insn.reg2i14_format.opcode) {
	case llw_op:
	case lld_op:
	case scw_op:
	case scd_op:
		pr_notice("ll and sc instructions are not supported\n");
		return true;
	}

	switch (insn.reg1i21_format.opcode) {
	case bceqz_op:
		pr_notice("bceqz and bcnez instructions are not supported\n");
		return true;
	}

	return false;
}

bool insns_need_simulation(union loongarch_instruction insn)
{
	if (is_pc_ins(&insn))
		return true;

	if (is_branch_ins(&insn))
		return true;

	return false;
}

void arch_simulate_insn(union loongarch_instruction insn, struct pt_regs *regs)
{
	if (is_pc_ins(&insn))
		simu_pc(regs, insn);
	else if (is_branch_ins(&insn))
		simu_branch(regs, insn);
}

int larch_insn_read(void *addr, u32 *insnp)
{
	int ret;
	u32 val;

	ret = copy_from_kernel_nofault(&val, addr, LOONGARCH_INSN_SIZE);
	if (!ret)
		*insnp = val;

	return ret;
}

int larch_insn_write(void *addr, u32 insn)
{
	int ret;
	unsigned long flags = 0;

	raw_spin_lock_irqsave(&patch_lock, flags);
	ret = copy_to_kernel_nofault(addr, &insn, LOONGARCH_INSN_SIZE);
	raw_spin_unlock_irqrestore(&patch_lock, flags);

	return ret;
}

int larch_insn_patch_text(void *addr, u32 insn)
{
	int ret;
	u32 *tp = addr;

	if ((unsigned long)tp & 3)
		return -EINVAL;

	ret = larch_insn_write(tp, insn);
	if (!ret)
		flush_icache_range((unsigned long)tp,
				   (unsigned long)tp + LOONGARCH_INSN_SIZE);

	return ret;
}

u32 larch_insn_gen_nop(void)
{
	return INSN_NOP;
}

u32 larch_insn_gen_b(unsigned long pc, unsigned long dest)
{
	long offset = dest - pc;
	union loongarch_instruction insn;

	if ((offset & 3) || offset < -SZ_128M || offset >= SZ_128M) {
		pr_warn("The generated b instruction is out of range.\n");
		return INSN_BREAK;
	}

	emit_b(&insn, offset >> 2);

	return insn.word;
}

u32 larch_insn_gen_bl(unsigned long pc, unsigned long dest)
{
	long offset = dest - pc;
	union loongarch_instruction insn;

	if ((offset & 3) || offset < -SZ_128M || offset >= SZ_128M) {
		pr_warn("The generated bl instruction is out of range.\n");
		return INSN_BREAK;
	}

	emit_bl(&insn, offset >> 2);

	return insn.word;
}

u32 larch_insn_gen_or(enum loongarch_gpr rd, enum loongarch_gpr rj, enum loongarch_gpr rk)
{
	union loongarch_instruction insn;

	emit_or(&insn, rd, rj, rk);

	return insn.word;
}

u32 larch_insn_gen_move(enum loongarch_gpr rd, enum loongarch_gpr rj)
{
	return larch_insn_gen_or(rd, rj, 0);
}

u32 larch_insn_gen_lu12iw(enum loongarch_gpr rd, int imm)
{
	union loongarch_instruction insn;

	if (imm < -SZ_512K || imm >= SZ_512K) {
		pr_warn("The generated lu12i.w instruction is out of range.\n");
		return INSN_BREAK;
	}

	emit_lu12iw(&insn, rd, imm);

	return insn.word;
}

u32 larch_insn_gen_lu32id(enum loongarch_gpr rd, int imm)
{
	union loongarch_instruction insn;

	if (imm < -SZ_512K || imm >= SZ_512K) {
		pr_warn("The generated lu32i.d instruction is out of range.\n");
		return INSN_BREAK;
	}

	emit_lu32id(&insn, rd, imm);

	return insn.word;
}

u32 larch_insn_gen_lu52id(enum loongarch_gpr rd, enum loongarch_gpr rj, int imm)
{
	union loongarch_instruction insn;

	if (imm < -SZ_2K || imm >= SZ_2K) {
		pr_warn("The generated lu52i.d instruction is out of range.\n");
		return INSN_BREAK;
	}

	emit_lu52id(&insn, rd, rj, imm);

	return insn.word;
}

u32 larch_insn_gen_jirl(enum loongarch_gpr rd, enum loongarch_gpr rj, int imm)
{
	union loongarch_instruction insn;

	if ((imm & 3) || imm < -SZ_128K || imm >= SZ_128K) {
		pr_warn("The generated jirl instruction is out of range.\n");
		return INSN_BREAK;
	}

	emit_jirl(&insn, rj, rd, imm >> 2);

	return insn.word;
}
