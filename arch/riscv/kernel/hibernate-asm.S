/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Hibernate support specific for RISCV
 *
 * Copyright (C) 2022 Shanghai StarFive Technology Co., Ltd.
 *
 * Author: Jee Heng Sia <jeeheng.sia@starfivetech.com>
 *
 */


#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/csr.h>

	.text
	.altmacro
	.option norelax

ENTRY(__hibernate_cpu_resume)
	/* Load the global pointer */
	.option push
	.option norelax
	la gp, __global_pointer$
	.option pop

        /* switch to root  page table */
        csrw CSR_SATP, s0
        sfence.vma

	ld	a0, hibernate_cpu_context

	/* Restore CSRs */
	REG_L   t0, (SUSPEND_CONTEXT_REGS + PT_EPC)(a0)
	csrw    CSR_EPC, t0
	REG_L   t0, (SUSPEND_CONTEXT_REGS + PT_STATUS)(a0)
	csrw    CSR_STATUS, t0
	REG_L   t0, (SUSPEND_CONTEXT_REGS + PT_BADADDR)(a0)
	csrw    CSR_TVAL, t0
	REG_L   t0, (SUSPEND_CONTEXT_REGS + PT_CAUSE)(a0)
	csrw    CSR_CAUSE, t0

	/* Restore registers (except A0 and T0-T6) */
	REG_L   ra, (SUSPEND_CONTEXT_REGS + PT_RA)(a0)
	REG_L   sp, (SUSPEND_CONTEXT_REGS + PT_SP)(a0)
	REG_L   gp, (SUSPEND_CONTEXT_REGS + PT_GP)(a0)
	REG_L   tp, (SUSPEND_CONTEXT_REGS + PT_TP)(a0)

	REG_L   s0, (SUSPEND_CONTEXT_REGS + PT_S0)(a0)
	REG_L   s1, (SUSPEND_CONTEXT_REGS + PT_S1)(a0)
	REG_L   a1, (SUSPEND_CONTEXT_REGS + PT_A1)(a0)
	REG_L   a2, (SUSPEND_CONTEXT_REGS + PT_A2)(a0)
	REG_L   a3, (SUSPEND_CONTEXT_REGS + PT_A3)(a0)
	REG_L   a4, (SUSPEND_CONTEXT_REGS + PT_A4)(a0)
	REG_L   a5, (SUSPEND_CONTEXT_REGS + PT_A5)(a0)
	REG_L   a6, (SUSPEND_CONTEXT_REGS + PT_A6)(a0)
	REG_L   a7, (SUSPEND_CONTEXT_REGS + PT_A7)(a0)
	REG_L   s2, (SUSPEND_CONTEXT_REGS + PT_S2)(a0)
	REG_L   s3, (SUSPEND_CONTEXT_REGS + PT_S3)(a0)
	REG_L   s4, (SUSPEND_CONTEXT_REGS + PT_S4)(a0)
	REG_L   s5, (SUSPEND_CONTEXT_REGS + PT_S5)(a0)
	REG_L   s6, (SUSPEND_CONTEXT_REGS + PT_S6)(a0)
	REG_L   s7, (SUSPEND_CONTEXT_REGS + PT_S7)(a0)
	REG_L   s8, (SUSPEND_CONTEXT_REGS + PT_S8)(a0)
	REG_L   s9, (SUSPEND_CONTEXT_REGS + PT_S9)(a0)
	REG_L   s10, (SUSPEND_CONTEXT_REGS + PT_S10)(a0)
	REG_L   s11, (SUSPEND_CONTEXT_REGS + PT_S11)(a0)

	/* Return zero value */
	add     a0, zero, zero

	ret
END(__hibernate_cpu_resume)

/* a0: satp of saved page tables
 * a1: satp of temporary page tables
 * a2: cpu_resume
 * a3: saved cpu_context
 */
ENTRY(restore_image)
	mv	s0, a0
	mv	s1, a1
	mv	s2, a2
	mv	s3, a3
	ld      s4, restore_pblist
	ld	a1, relocated_restore_code

	jalr	a1
END(restore_image)

ENTRY(core_restore_code)
	/* switch to temp page table */
	csrw satp, s1
	sfence.vma
	beqz	s4, done
loop:
	ld	a1, HIBERN_PBE_ADDR(s4)
	ld	a0, HIBERN_PBE_ORIG(s4)

	lui     a4, 0x1
	add     a4, a4, a0
copy:	ld      a5, 0(a1)
	addi    a0, a0, 8
	addi    a1, a1, 8
	sd      a5, -8(a0)
	bne     a4, a0, copy

	ld	s4, HIBERN_PBE_NEXT(s4)
	bnez	s4, loop
done:
	mv	a0, s3
	jalr	s2
END(core_restore_code)
