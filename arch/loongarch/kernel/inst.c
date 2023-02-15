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

	emit_lu12iw(&insn, rd, imm);

	return insn.word;
}

u32 larch_insn_gen_lu32id(enum loongarch_gpr rd, int imm)
{
	union loongarch_instruction insn;

	emit_lu32id(&insn, rd, imm);

	return insn.word;
}

u32 larch_insn_gen_lu52id(enum loongarch_gpr rd, enum loongarch_gpr rj, int imm)
{
	union loongarch_instruction insn;

	emit_lu52id(&insn, rd, rj, imm);

	return insn.word;
}

u32 larch_insn_gen_jirl(enum loongarch_gpr rd, enum loongarch_gpr rj, unsigned long pc, unsigned long dest)
{
	union loongarch_instruction insn;

	emit_jirl(&insn, rj, rd, (dest - pc) >> 2);

	return insn.word;
}
