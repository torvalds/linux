/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 FORTH-ICS/CARV
 *  Nick Kossifidis <mick@ics.forth.gr>
 */

#include <asm/asm.h>    	/* For RISCV_* and REG_* macros */
#include <asm/csr.h>		/* For CSR_* macros */
#include <asm/asm-offsets.h>	/* For offsets on pt_regs */
#include <linux/linkage.h>	/* For SYM_* macros */

.section ".text"
SYM_CODE_START(riscv_crash_save_regs)
	REG_S ra,  PT_RA(a0)	/* x1 */
	REG_S sp,  PT_SP(a0)	/* x2 */
	REG_S gp,  PT_GP(a0)	/* x3 */
	REG_S tp,  PT_TP(a0)	/* x4 */
	REG_S t0,  PT_T0(a0)	/* x5 */
	REG_S t1,  PT_T1(a0)	/* x6 */
	REG_S t2,  PT_T2(a0)	/* x7 */
	REG_S s0,  PT_S0(a0)	/* x8/fp */
	REG_S s1,  PT_S1(a0)	/* x9 */
	REG_S a0,  PT_A0(a0)	/* x10 */
	REG_S a1,  PT_A1(a0)	/* x11 */
	REG_S a2,  PT_A2(a0)	/* x12 */
	REG_S a3,  PT_A3(a0)	/* x13 */
	REG_S a4,  PT_A4(a0)	/* x14 */
	REG_S a5,  PT_A5(a0)	/* x15 */
	REG_S a6,  PT_A6(a0)	/* x16 */
	REG_S a7,  PT_A7(a0)	/* x17 */
	REG_S s2,  PT_S2(a0)	/* x18 */
	REG_S s3,  PT_S3(a0)	/* x19 */
	REG_S s4,  PT_S4(a0)	/* x20 */
	REG_S s5,  PT_S5(a0)	/* x21 */
	REG_S s6,  PT_S6(a0)	/* x22 */
	REG_S s7,  PT_S7(a0)	/* x23 */
	REG_S s8,  PT_S8(a0)	/* x24 */
	REG_S s9,  PT_S9(a0)	/* x25 */
	REG_S s10, PT_S10(a0)	/* x26 */
	REG_S s11, PT_S11(a0)	/* x27 */
	REG_S t3,  PT_T3(a0)	/* x28 */
	REG_S t4,  PT_T4(a0)	/* x29 */
	REG_S t5,  PT_T5(a0)	/* x30 */
	REG_S t6,  PT_T6(a0)	/* x31 */

	csrr t1, CSR_STATUS
	csrr t2, CSR_EPC
	csrr t3, CSR_TVAL
	csrr t4, CSR_CAUSE

	REG_S t1, PT_STATUS(a0)
	REG_S t2, PT_EPC(a0)
	REG_S t3, PT_BADADDR(a0)
	REG_S t4, PT_CAUSE(a0)
	ret
SYM_CODE_END(riscv_crash_save_regs)
