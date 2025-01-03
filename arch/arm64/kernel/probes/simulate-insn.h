/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm64/kernel/probes/simulate-insn.h
 *
 * Copyright (C) 2013 Linaro Limited
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
void simulate_nop(u32 opcode, long addr, struct pt_regs *regs);

#endif /* _ARM_KERNEL_KPROBES_SIMULATE_INSN_H */
