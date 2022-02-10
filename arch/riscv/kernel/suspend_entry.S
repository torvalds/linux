/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#include <linux/linkage.h>
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/csr.h>

	.text
	.altmacro
	.option norelax

ENTRY(__cpu_suspend_enter)
	/* Save registers (except A0 and T0-T6) */
	REG_S	ra, (SUSPEND_CONTEXT_REGS + PT_RA)(a0)
	REG_S	sp, (SUSPEND_CONTEXT_REGS + PT_SP)(a0)
	REG_S	gp, (SUSPEND_CONTEXT_REGS + PT_GP)(a0)
	REG_S	tp, (SUSPEND_CONTEXT_REGS + PT_TP)(a0)
	REG_S	s0, (SUSPEND_CONTEXT_REGS + PT_S0)(a0)
	REG_S	s1, (SUSPEND_CONTEXT_REGS + PT_S1)(a0)
	REG_S	a1, (SUSPEND_CONTEXT_REGS + PT_A1)(a0)
	REG_S	a2, (SUSPEND_CONTEXT_REGS + PT_A2)(a0)
	REG_S	a3, (SUSPEND_CONTEXT_REGS + PT_A3)(a0)
	REG_S	a4, (SUSPEND_CONTEXT_REGS + PT_A4)(a0)
	REG_S	a5, (SUSPEND_CONTEXT_REGS + PT_A5)(a0)
	REG_S	a6, (SUSPEND_CONTEXT_REGS + PT_A6)(a0)
	REG_S	a7, (SUSPEND_CONTEXT_REGS + PT_A7)(a0)
	REG_S	s2, (SUSPEND_CONTEXT_REGS + PT_S2)(a0)
	REG_S	s3, (SUSPEND_CONTEXT_REGS + PT_S3)(a0)
	REG_S	s4, (SUSPEND_CONTEXT_REGS + PT_S4)(a0)
	REG_S	s5, (SUSPEND_CONTEXT_REGS + PT_S5)(a0)
	REG_S	s6, (SUSPEND_CONTEXT_REGS + PT_S6)(a0)
	REG_S	s7, (SUSPEND_CONTEXT_REGS + PT_S7)(a0)
	REG_S	s8, (SUSPEND_CONTEXT_REGS + PT_S8)(a0)
	REG_S	s9, (SUSPEND_CONTEXT_REGS + PT_S9)(a0)
	REG_S	s10, (SUSPEND_CONTEXT_REGS + PT_S10)(a0)
	REG_S	s11, (SUSPEND_CONTEXT_REGS + PT_S11)(a0)

	/* Save CSRs */
	csrr	t0, CSR_EPC
	REG_S	t0, (SUSPEND_CONTEXT_REGS + PT_EPC)(a0)
	csrr	t0, CSR_STATUS
	REG_S	t0, (SUSPEND_CONTEXT_REGS + PT_STATUS)(a0)
	csrr	t0, CSR_TVAL
	REG_S	t0, (SUSPEND_CONTEXT_REGS + PT_BADADDR)(a0)
	csrr	t0, CSR_CAUSE
	REG_S	t0, (SUSPEND_CONTEXT_REGS + PT_CAUSE)(a0)

	/* Return non-zero value */
	li	a0, 1

	/* Return to C code */
	ret
END(__cpu_suspend_enter)

ENTRY(__cpu_resume_enter)
	/* Load the global pointer */
	.option push
	.option norelax
		la gp, __global_pointer$
	.option pop

#ifdef CONFIG_MMU
	/* Save A0 and A1 */
	add	t0, a0, zero
	add	t1, a1, zero

	/* Enable MMU */
	la	a0, swapper_pg_dir
	XIP_FIXUP_OFFSET a0
	call	relocate_enable_mmu

	/* Restore A0 and A1 */
	add	a0, t0, zero
	add	a1, t1, zero
#endif

	/* Make A0 point to suspend context */
	add	a0, a1, zero

	/* Restore CSRs */
	REG_L	t0, (SUSPEND_CONTEXT_REGS + PT_EPC)(a0)
	csrw	CSR_EPC, t0
	REG_L	t0, (SUSPEND_CONTEXT_REGS + PT_STATUS)(a0)
	csrw	CSR_STATUS, t0
	REG_L	t0, (SUSPEND_CONTEXT_REGS + PT_BADADDR)(a0)
	csrw	CSR_TVAL, t0
	REG_L	t0, (SUSPEND_CONTEXT_REGS + PT_CAUSE)(a0)
	csrw	CSR_CAUSE, t0

	/* Restore registers (except A0 and T0-T6) */
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

	/* Return zero value */
	add	a0, zero, zero

	/* Return to C code */
	ret
END(__cpu_resume_enter)
