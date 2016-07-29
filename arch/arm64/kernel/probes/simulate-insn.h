/*
 * arch/arm64/kernel/probes/simulate-insn.h
 *
 * Copyright (C) 2013 Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _ARM_KERNEL_KPROBES_SIMULATE_INSN_H
#define _ARM_KERNEL_KPROBES_SIMULATE_INSN_H

void simulate_adr_adrp(u32 opcode, long addr, struct pt_regs *regs);
void simulate_b_bl(u32 opcode, long addr, struct pt_regs *regs);
void simulate_b_cond(u32 opcode, long addr, struct pt_regs *regs);
void simulate_br_blr_ret(u32 opcode, long addr, struct pt_regs *regs);
void simulate_cbz_cbnz(u32 opcode, long addr, struct pt_regs *regs);
void simulate_tbz_tbnz(u32 opcode, long addr, struct pt_regs *regs);
void simulate_ldr_literal(u32 opcode, long addr, struct pt_regs *regs);
void simulate_ldrsw_literal(u32 opcode, long addr, struct pt_regs *regs);

#endif /* _ARM_KERNEL_KPROBES_SIMULATE_INSN_H */
