// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm64/kernel/probes/simulate-insn.c
 *
 * Copyright (C) 2013 Linaro Limited.
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>

#include <asm/ptrace.h>

#include "simulate-insn.h"

#define bbl_displacement(insn)		\
	sign_extend32(((insn) & 0x3ffffff) << 2, 27)

#define bcond_displacement(insn)	\
	sign_extend32(((insn >> 5) & 0x7ffff) << 2, 20)

#define cbz_displacement(insn)	\
	sign_extend32(((insn >> 5) & 0x7ffff) << 2, 20)

#define tbz_displacement(insn)	\
	sign_extend32(((insn >> 5) & 0x3fff) << 2, 15)

#define ldr_displacement(insn)	\
	sign_extend32(((insn >> 5) & 0x7ffff) << 2, 20)

static inline void set_x_reg(struct pt_regs *regs, int reg, u64 val)
{
	pt_regs_write_reg(regs, reg, val);
}

static inline void set_w_reg(struct pt_regs *regs, int reg, u64 val)
{
	pt_regs_write_reg(regs, reg, lower_32_bits(val));
}

static inline u64 get_x_reg(struct pt_regs *regs, int reg)
{
	return pt_regs_read_reg(regs, reg);
}

static inline u32 get_w_reg(struct pt_regs *regs, int reg)
{
	return lower_32_bits(pt_regs_read_reg(regs, reg));
}

static bool __kprobes check_cbz(u32 opcode, struct pt_regs *regs)
{
	int xn = opcode & 0x1f;

	return (opcode & (1 << 31)) ?
	    (get_x_reg(regs, xn) == 0) : (get_w_reg(regs, xn) == 0);
}

static bool __kprobes check_cbnz(u32 opcode, struct pt_regs *regs)
{
	int xn = opcode & 0x1f;

	return (opcode & (1 << 31)) ?
	    (get_x_reg(regs, xn) != 0) : (get_w_reg(regs, xn) != 0);
}

static bool __kprobes check_tbz(u32 opcode, struct pt_regs *regs)
{
	int xn = opcode & 0x1f;
	int bit_pos = ((opcode & (1 << 31)) >> 26) | ((opcode >> 19) & 0x1f);

	return ((get_x_reg(regs, xn) >> bit_pos) & 0x1) == 0;
}

static bool __kprobes check_tbnz(u32 opcode, struct pt_regs *regs)
{
	int xn = opcode & 0x1f;
	int bit_pos = ((opcode & (1 << 31)) >> 26) | ((opcode >> 19) & 0x1f);

	return ((get_x_reg(regs, xn) >> bit_pos) & 0x1) != 0;
}

/*
 * instruction simulation functions
 */
void __kprobes
simulate_adr_adrp(u32 opcode, long addr, struct pt_regs *regs)
{
	long imm, xn, val;

	xn = opcode & 0x1f;
	imm = ((opcode >> 3) & 0x1ffffc) | ((opcode >> 29) & 0x3);
	imm = sign_extend64(imm, 20);
	if (opcode & 0x80000000)
		val = (imm<<12) + (addr & 0xfffffffffffff000);
	else
		val = imm + addr;

	set_x_reg(regs, xn, val);

	instruction_pointer_set(regs, instruction_pointer(regs) + 4);
}

void __kprobes
simulate_b_bl(u32 opcode, long addr, struct pt_regs *regs)
{
	int disp = bbl_displacement(opcode);

	/* Link register is x30 */
	if (opcode & (1 << 31))
		set_x_reg(regs, 30, addr + 4);

	instruction_pointer_set(regs, addr + disp);
}

void __kprobes
simulate_b_cond(u32 opcode, long addr, struct pt_regs *regs)
{
	int disp = 4;

	if (aarch32_opcode_cond_checks[opcode & 0xf](regs->pstate & 0xffffffff))
		disp = bcond_displacement(opcode);

	instruction_pointer_set(regs, addr + disp);
}

void __kprobes
simulate_br_blr_ret(u32 opcode, long addr, struct pt_regs *regs)
{
	int xn = (opcode >> 5) & 0x1f;

	/* update pc first in case we're doing a "blr lr" */
	instruction_pointer_set(regs, get_x_reg(regs, xn));

	/* Link register is x30 */
	if (((opcode >> 21) & 0x3) == 1)
		set_x_reg(regs, 30, addr + 4);
}

void __kprobes
simulate_cbz_cbnz(u32 opcode, long addr, struct pt_regs *regs)
{
	int disp = 4;

	if (opcode & (1 << 24)) {
		if (check_cbnz(opcode, regs))
			disp = cbz_displacement(opcode);
	} else {
		if (check_cbz(opcode, regs))
			disp = cbz_displacement(opcode);
	}
	instruction_pointer_set(regs, addr + disp);
}

void __kprobes
simulate_tbz_tbnz(u32 opcode, long addr, struct pt_regs *regs)
{
	int disp = 4;

	if (opcode & (1 << 24)) {
		if (check_tbnz(opcode, regs))
			disp = tbz_displacement(opcode);
	} else {
		if (check_tbz(opcode, regs))
			disp = tbz_displacement(opcode);
	}
	instruction_pointer_set(regs, addr + disp);
}

void __kprobes
simulate_ldr_literal(u32 opcode, long addr, struct pt_regs *regs)
{
	u64 *load_addr;
	int xn = opcode & 0x1f;
	int disp;

	disp = ldr_displacement(opcode);
	load_addr = (u64 *) (addr + disp);

	if (opcode & (1 << 30))	/* x0-x30 */
		set_x_reg(regs, xn, *load_addr);
	else			/* w0-w30 */
		set_w_reg(regs, xn, *load_addr);

	instruction_pointer_set(regs, instruction_pointer(regs) + 4);
}

void __kprobes
simulate_ldrsw_literal(u32 opcode, long addr, struct pt_regs *regs)
{
	s32 *load_addr;
	int xn = opcode & 0x1f;
	int disp;

	disp = ldr_displacement(opcode);
	load_addr = (s32 *) (addr + disp);

	set_x_reg(regs, xn, *load_addr);

	instruction_pointer_set(regs, instruction_pointer(regs) + 4);
}
