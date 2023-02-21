// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/sizes.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/inst.h>

static DEFINE_RAW_SPINLOCK(patch_lock);

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
	unsigned int immediate_l, immediate_h;
	union loongarch_instruction insn;

	if ((offset & 3) || offset < -SZ_128M || offset >= SZ_128M) {
		pr_warn("The generated b instruction is out of range.\n");
		return INSN_BREAK;
	}

	offset >>= 2;

	immediate_l = offset & 0xffff;
	offset >>= 16;
	immediate_h = offset & 0x3ff;

	insn.reg0i26_format.opcode = b_op;
	insn.reg0i26_format.immediate_l = immediate_l;
	insn.reg0i26_format.immediate_h = immediate_h;

	return insn.word;
}

u32 larch_insn_gen_bl(unsigned long pc, unsigned long dest)
{
	long offset = dest - pc;
	unsigned int immediate_l, immediate_h;
	union loongarch_instruction insn;

	if ((offset & 3) || offset < -SZ_128M || offset >= SZ_128M) {
		pr_warn("The generated bl instruction is out of range.\n");
		return INSN_BREAK;
	}

	offset >>= 2;

	immediate_l = offset & 0xffff;
	offset >>= 16;
	immediate_h = offset & 0x3ff;

	insn.reg0i26_format.opcode = bl_op;
	insn.reg0i26_format.immediate_l = immediate_l;
	insn.reg0i26_format.immediate_h = immediate_h;

	return insn.word;
}

u32 larch_insn_gen_or(enum loongarch_gpr rd, enum loongarch_gpr rj, enum loongarch_gpr rk)
{
	union loongarch_instruction insn;

	insn.reg3_format.opcode = or_op;
	insn.reg3_format.rd = rd;
	insn.reg3_format.rj = rj;
	insn.reg3_format.rk = rk;

	return insn.word;
}

u32 larch_insn_gen_move(enum loongarch_gpr rd, enum loongarch_gpr rj)
{
	return larch_insn_gen_or(rd, rj, 0);
}

u32 larch_insn_gen_lu12iw(enum loongarch_gpr rd, int imm)
{
	union loongarch_instruction insn;

	insn.reg1i20_format.opcode = lu12iw_op;
	insn.reg1i20_format.rd = rd;
	insn.reg1i20_format.immediate = imm;

	return insn.word;
}

u32 larch_insn_gen_lu32id(enum loongarch_gpr rd, int imm)
{
	union loongarch_instruction insn;

	insn.reg1i20_format.opcode = lu32id_op;
	insn.reg1i20_format.rd = rd;
	insn.reg1i20_format.immediate = imm;

	return insn.word;
}

u32 larch_insn_gen_lu52id(enum loongarch_gpr rd, enum loongarch_gpr rj, int imm)
{
	union loongarch_instruction insn;

	insn.reg2i12_format.opcode = lu52id_op;
	insn.reg2i12_format.rd = rd;
	insn.reg2i12_format.rj = rj;
	insn.reg2i12_format.immediate = imm;

	return insn.word;
}

u32 larch_insn_gen_jirl(enum loongarch_gpr rd, enum loongarch_gpr rj, unsigned long pc, unsigned long dest)
{
	union loongarch_instruction insn;

	insn.reg2i16_format.opcode = jirl_op;
	insn.reg2i16_format.rd = rd;
	insn.reg2i16_format.rj = rj;
	insn.reg2i16_format.immediate = (dest - pc) >> 2;

	return insn.word;
}
