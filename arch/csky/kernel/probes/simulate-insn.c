// SPDX-License-Identifier: GPL-2.0+

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>

#include "decode-insn.h"
#include "simulate-insn.h"

static inline bool csky_insn_reg_get_val(struct pt_regs *regs,
					 unsigned long index,
					 unsigned long *ptr)
{
	if (index < 14)
		*ptr = *(&regs->a0 + index);

	if (index > 15 && index < 31)
		*ptr = *(&regs->exregs[0] + index - 16);

	switch (index) {
	case 14:
		*ptr = regs->usp;
		break;
	case 15:
		*ptr = regs->lr;
		break;
	case 31:
		*ptr = regs->tls;
		break;
	default:
		goto fail;
	}

	return true;
fail:
	return false;
}

static inline bool csky_insn_reg_set_val(struct pt_regs *regs,
					 unsigned long index,
					 unsigned long val)
{
	if (index < 14)
		*(&regs->a0 + index) = val;

	if (index > 15 && index < 31)
		*(&regs->exregs[0] + index - 16) = val;

	switch (index) {
	case 14:
		regs->usp = val;
		break;
	case 15:
		regs->lr = val;
		break;
	case 31:
		regs->tls = val;
		break;
	default:
		goto fail;
	}

	return true;
fail:
	return false;
}

void __kprobes
simulate_br16(u32 opcode, long addr, struct pt_regs *regs)
{
	instruction_pointer_set(regs,
		addr + sign_extend32((opcode & 0x3ff) << 1, 9));
}

void __kprobes
simulate_br32(u32 opcode, long addr, struct pt_regs *regs)
{
	instruction_pointer_set(regs,
		addr + sign_extend32((opcode & 0xffff0000) >> 15, 15));
}

void __kprobes
simulate_bt16(u32 opcode, long addr, struct pt_regs *regs)
{
	if (regs->sr & 1)
		instruction_pointer_set(regs,
			addr + sign_extend32((opcode & 0x3ff) << 1, 9));
	else
		instruction_pointer_set(regs, addr + 2);
}

void __kprobes
simulate_bt32(u32 opcode, long addr, struct pt_regs *regs)
{
	if (regs->sr & 1)
		instruction_pointer_set(regs,
			addr + sign_extend32((opcode & 0xffff0000) >> 15, 15));
	else
		instruction_pointer_set(regs, addr + 4);
}

void __kprobes
simulate_bf16(u32 opcode, long addr, struct pt_regs *regs)
{
	if (!(regs->sr & 1))
		instruction_pointer_set(regs,
			addr + sign_extend32((opcode & 0x3ff) << 1, 9));
	else
		instruction_pointer_set(regs, addr + 2);
}

void __kprobes
simulate_bf32(u32 opcode, long addr, struct pt_regs *regs)
{
	if (!(regs->sr & 1))
		instruction_pointer_set(regs,
			addr + sign_extend32((opcode & 0xffff0000) >> 15, 15));
	else
		instruction_pointer_set(regs, addr + 4);
}

void __kprobes
simulate_jmp16(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long tmp = (opcode >> 2) & 0xf;

	csky_insn_reg_get_val(regs, tmp, &tmp);

	instruction_pointer_set(regs, tmp & 0xfffffffe);
}

void __kprobes
simulate_jmp32(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long tmp = opcode & 0x1f;

	csky_insn_reg_get_val(regs, tmp, &tmp);

	instruction_pointer_set(regs, tmp & 0xfffffffe);
}

void __kprobes
simulate_jsr16(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long tmp = (opcode >> 2) & 0xf;

	csky_insn_reg_get_val(regs, tmp, &tmp);

	regs->lr = addr + 2;

	instruction_pointer_set(regs, tmp & 0xfffffffe);
}

void __kprobes
simulate_jsr32(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long tmp = opcode & 0x1f;

	csky_insn_reg_get_val(regs, tmp, &tmp);

	regs->lr = addr + 4;

	instruction_pointer_set(regs, tmp & 0xfffffffe);
}

void __kprobes
simulate_lrw16(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long val;
	unsigned long tmp = (opcode & 0x300) >> 3;
	unsigned long offset = ((opcode & 0x1f) | tmp) << 2;

	tmp = (opcode & 0xe0) >> 5;

	val = *(unsigned int *)(instruction_pointer(regs) + offset);

	csky_insn_reg_set_val(regs, tmp, val);
}

void __kprobes
simulate_lrw32(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long val;
	unsigned long offset = (opcode & 0xffff0000) >> 14;
	unsigned long tmp = opcode & 0x0000001f;

	val = *(unsigned int *)
		((instruction_pointer(regs) + offset) & 0xfffffffc);

	csky_insn_reg_set_val(regs, tmp, val);
}

void __kprobes
simulate_pop16(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long *tmp = (unsigned long *)regs->usp;
	int i;

	for (i = 0; i < (opcode & 0xf); i++) {
		csky_insn_reg_set_val(regs, i + 4, *tmp);
		tmp += 1;
	}

	if (opcode & 0x10) {
		csky_insn_reg_set_val(regs, 15, *tmp);
		tmp += 1;
	}

	regs->usp = (unsigned long)tmp;

	instruction_pointer_set(regs, regs->lr);
}

void __kprobes
simulate_pop32(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long *tmp = (unsigned long *)regs->usp;
	int i;

	for (i = 0; i < ((opcode & 0xf0000) >> 16); i++) {
		csky_insn_reg_set_val(regs, i + 4, *tmp);
		tmp += 1;
	}

	if (opcode & 0x100000) {
		csky_insn_reg_set_val(regs, 15, *tmp);
		tmp += 1;
	}

	for (i = 0; i < ((opcode & 0xe00000) >> 21); i++) {
		csky_insn_reg_set_val(regs, i + 16, *tmp);
		tmp += 1;
	}

	if (opcode & 0x1000000) {
		csky_insn_reg_set_val(regs, 29, *tmp);
		tmp += 1;
	}

	regs->usp = (unsigned long)tmp;

	instruction_pointer_set(regs, regs->lr);
}

void __kprobes
simulate_bez32(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long tmp = opcode & 0x1f;

	csky_insn_reg_get_val(regs, tmp, &tmp);

	if (tmp == 0) {
		instruction_pointer_set(regs,
			addr + sign_extend32((opcode & 0xffff0000) >> 15, 15));
	} else
		instruction_pointer_set(regs, addr + 4);
}

void __kprobes
simulate_bnez32(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long tmp = opcode & 0x1f;

	csky_insn_reg_get_val(regs, tmp, &tmp);

	if (tmp != 0) {
		instruction_pointer_set(regs,
			addr + sign_extend32((opcode & 0xffff0000) >> 15, 15));
	} else
		instruction_pointer_set(regs, addr + 4);
}

void __kprobes
simulate_bnezad32(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long tmp = opcode & 0x1f;
	unsigned long val;

	csky_insn_reg_get_val(regs, tmp, &val);

	val -= 1;

	if (val > 0) {
		instruction_pointer_set(regs,
			addr + sign_extend32((opcode & 0xffff0000) >> 15, 15));
	} else
		instruction_pointer_set(regs, addr + 4);

	csky_insn_reg_set_val(regs, tmp, val);
}

void __kprobes
simulate_bhsz32(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long tmp = opcode & 0x1f;
	unsigned long val;

	csky_insn_reg_get_val(regs, tmp, &val);

	if (val >= 0) {
		instruction_pointer_set(regs,
			addr + sign_extend32((opcode & 0xffff0000) >> 15, 15));
	} else
		instruction_pointer_set(regs, addr + 4);

	csky_insn_reg_set_val(regs, tmp, val);
}

void __kprobes
simulate_bhz32(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long tmp = opcode & 0x1f;
	unsigned long val;

	csky_insn_reg_get_val(regs, tmp, &val);

	if (val > 0) {
		instruction_pointer_set(regs,
			addr + sign_extend32((opcode & 0xffff0000) >> 15, 15));
	} else
		instruction_pointer_set(regs, addr + 4);

	csky_insn_reg_set_val(regs, tmp, val);
}

void __kprobes
simulate_blsz32(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long tmp = opcode & 0x1f;
	unsigned long val;

	csky_insn_reg_get_val(regs, tmp, &val);

	if (val <= 0) {
		instruction_pointer_set(regs,
			addr + sign_extend32((opcode & 0xffff0000) >> 15, 15));
	} else
		instruction_pointer_set(regs, addr + 4);

	csky_insn_reg_set_val(regs, tmp, val);
}

void __kprobes
simulate_blz32(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long tmp = opcode & 0x1f;
	unsigned long val;

	csky_insn_reg_get_val(regs, tmp, &val);

	if (val < 0) {
		instruction_pointer_set(regs,
			addr + sign_extend32((opcode & 0xffff0000) >> 15, 15));
	} else
		instruction_pointer_set(regs, addr + 4);

	csky_insn_reg_set_val(regs, tmp, val);
}

void __kprobes
simulate_bsr32(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long tmp;

	tmp = (opcode & 0xffff) << 16;
	tmp |= (opcode & 0xffff0000) >> 16;

	instruction_pointer_set(regs,
		addr + sign_extend32((tmp & 0x3ffffff) << 1, 15));

	regs->lr = addr + 4;
}

void __kprobes
simulate_jmpi32(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long val;
	unsigned long offset = ((opcode & 0xffff0000) >> 14);

	val = *(unsigned int *)
		((instruction_pointer(regs) + offset) & 0xfffffffc);

	instruction_pointer_set(regs, val);
}

void __kprobes
simulate_jsri32(u32 opcode, long addr, struct pt_regs *regs)
{
	unsigned long val;
	unsigned long offset = ((opcode & 0xffff0000) >> 14);

	val = *(unsigned int *)
		((instruction_pointer(regs) + offset) & 0xfffffffc);

	regs->lr = addr + 4;

	instruction_pointer_set(regs, val);
}
