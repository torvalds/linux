/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 Feng Jiang <jiangfeng@kylinos.cn> */

#include <linux/linkage.h>
#include <asm/asm.h>

/*
 * Trampoline for the BPF timed may_goto loop bound. Custom calling convention:
 *	- input:  stack offset in BPF_REG_AX (t0)
 *	- output: updated count in BPF_REG_AX (t0)
 *
 * Calls bpf_check_timed_may_goto(ptr) with the standard RISC-V ABI, where
 * ptr = BPF_REG_FP (s5) + BPF_REG_AX (t0). BPF R0-R5 (t6, a0-a4) and stack
 * arguments 6-8 (a5-a7) are saved across the call; BPF_REG_FP (s5) is
 * callee-saved and needs no saving.
 */

SYM_FUNC_START(arch_bpf_timed_may_goto)
	addi	sp, sp, -(12*SZREG)
	REG_S	ra, 11*SZREG(sp)
	REG_S	s0, 10*SZREG(sp)
	addi	s0, sp, 12*SZREG

	/* Save BPF registers R0-R5 (t6, a0-a4) */
	REG_S	t6, 5*SZREG(sp)
	REG_S	a0, 4*SZREG(sp)
	REG_S	a1, 3*SZREG(sp)
	REG_S	a2, 2*SZREG(sp)
	REG_S	a3, 1*SZREG(sp)
	REG_S	a4, 0*SZREG(sp)
	/* Save stack arguments 6-8 (a5-a7) */
	REG_S	a5, 6*SZREG(sp)
	REG_S	a6, 7*SZREG(sp)
	REG_S	a7, 8*SZREG(sp)

	add	a0, t0, s5
	call	bpf_check_timed_may_goto
	mv	t0, a0

	/* Restore BPF registers R0-R5 */
	REG_L	a4, 0*SZREG(sp)
	REG_L	a3, 1*SZREG(sp)
	REG_L	a2, 2*SZREG(sp)
	REG_L	a1, 3*SZREG(sp)
	REG_L	a0, 4*SZREG(sp)
	REG_L	t6, 5*SZREG(sp)
	/* Restore stack arguments 6-8 */
	REG_L	a5, 6*SZREG(sp)
	REG_L	a6, 7*SZREG(sp)
	REG_L	a7, 8*SZREG(sp)

	REG_L	s0, 10*SZREG(sp)
	REG_L	ra, 11*SZREG(sp)
	addi	sp, sp, 12*SZREG
	ret
SYM_FUNC_END(arch_bpf_timed_may_goto)
