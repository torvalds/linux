/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 StarFive Technology Co., Ltd.
 *
 * Author: Jee Heng Sia <jeeheng.sia@starfivetech.com>
 */

#ifndef __ASSEMBLER__
#error "Only include this from assembly code"
#endif

#ifndef __ASM_ASSEMBLER_H
#define __ASM_ASSEMBLER_H

#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/csr.h>

/*
 * suspend_restore_csrs - restore CSRs
 */
	.macro suspend_restore_csrs
		REG_L	t0, (SUSPEND_CONTEXT_REGS + PT_EPC)(a0)
		csrw	CSR_EPC, t0
		REG_L	t0, (SUSPEND_CONTEXT_REGS + PT_STATUS)(a0)
		csrw	CSR_STATUS, t0
		REG_L	t0, (SUSPEND_CONTEXT_REGS + PT_BADADDR)(a0)
		csrw	CSR_TVAL, t0
		REG_L	t0, (SUSPEND_CONTEXT_REGS + PT_CAUSE)(a0)
		csrw	CSR_CAUSE, t0
	.endm

/*
 * suspend_restore_regs - Restore registers (except A0 and T0-T6)
 */
	.macro suspend_restore_regs
		REG_L	ra, (SUSPEND_CONTEXT_REGS + PT_RA)(a0)
		REG_L	sp, (SUSPEND_CONTEXT_REGS + PT_SP)(a0)
		REG_L	gp, (SUSPEND_CONTEXT_REGS + PT_GP)(a0)
		REG_L	tp, (SUSPEND_CONTEXT_REGS + PT_TP)(a0)
		REG_L	s0, (SUSPEND_CONTEXT_REGS + PT_S0)(a0)
		REG_L	s1, (SUSPEND_CONTEXT_REGS + PT_S1)(a0)
		REG_L	a1, (SUSPEND_CONTEXT_REGS + PT_A1)(a0)
		REG_L	a2, (SUSPEND_CONTEXT_REGS + PT_A2)(a0)
		REG_L	a3, (SUSPEND_CONTEXT_REGS + PT_A3)(a0)
		REG_L	a4, (SUSPEND_CONTEXT_REGS + PT_A4)(a0)
		REG_L	a5, (SUSPEND_CONTEXT_REGS + PT_A5)(a0)
		REG_L	a6, (SUSPEND_CONTEXT_REGS + PT_A6)(a0)
		REG_L	a7, (SUSPEND_CONTEXT_REGS + PT_A7)(a0)
		REG_L	s2, (SUSPEND_CONTEXT_REGS + PT_S2)(a0)
		REG_L	s3, (SUSPEND_CONTEXT_REGS + PT_S3)(a0)
		REG_L	s4, (SUSPEND_CONTEXT_REGS + PT_S4)(a0)
		REG_L	s5, (SUSPEND_CONTEXT_REGS + PT_S5)(a0)
		REG_L	s6, (SUSPEND_CONTEXT_REGS + PT_S6)(a0)
		REG_L	s7, (SUSPEND_CONTEXT_REGS + PT_S7)(a0)
		REG_L	s8, (SUSPEND_CONTEXT_REGS + PT_S8)(a0)
		REG_L	s9, (SUSPEND_CONTEXT_REGS + PT_S9)(a0)
		REG_L	s10, (SUSPEND_CONTEXT_REGS + PT_S10)(a0)
		REG_L	s11, (SUSPEND_CONTEXT_REGS + PT_S11)(a0)
	.endm

/*
 * copy_page - copy 1 page (4KB) of data from source to destination
 * @a0 - destination
 * @a1 - source
 */
	.macro	copy_page a0, a1
		lui	a2, 0x1
		add	a2, a2, a0
1 :
		REG_L	t0, 0(a1)
		REG_L	t1, SZREG(a1)

		REG_S	t0, 0(a0)
		REG_S	t1, SZREG(a0)

		addi	a0, a0, 2 * SZREG
		addi	a1, a1, 2 * SZREG
		bne	a2, a0, 1b
	.endm

#endif	/* __ASM_ASSEMBLER_H */
