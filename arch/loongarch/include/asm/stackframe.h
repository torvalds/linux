/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_STACKFRAME_H
#define _ASM_STACKFRAME_H

#include <linux/threads.h>

#include <asm/addrspace.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/asm-offsets.h>
#include <asm/loongarch.h>
#include <asm/thread_info.h>

/* Make the addition of cfi info a little easier. */
	.macro cfi_rel_offset reg offset=0 docfi=0
	.if \docfi
	.cfi_rel_offset \reg, \offset
	.endif
	.endm

	.macro cfi_st reg offset=0 docfi=0
	cfi_rel_offset \reg, \offset, \docfi
	LONG_S	\reg, sp, \offset
	.endm

	.macro cfi_restore reg offset=0 docfi=0
	.if \docfi
	.cfi_restore \reg
	.endif
	.endm

	.macro cfi_ld reg offset=0 docfi=0
	LONG_L	\reg, sp, \offset
	cfi_restore \reg \offset \docfi
	.endm

/* Jump to the runtime virtual address. */
	.macro JUMP_VIRT_ADDR temp1 temp2
	li.d	\temp1, CACHE_BASE
	pcaddi	\temp2, 0
	or	\temp1, \temp1, \temp2
	jirl	zero, \temp1, 0xc
	.endm

	.macro BACKUP_T0T1
	csrwr	t0, EXCEPTION_KS0
	csrwr	t1, EXCEPTION_KS1
	.endm

	.macro RELOAD_T0T1
	csrrd   t0, EXCEPTION_KS0
	csrrd   t1, EXCEPTION_KS1
	.endm

	.macro	SAVE_TEMP docfi=0
	RELOAD_T0T1
	cfi_st	t0, PT_R12, \docfi
	cfi_st	t1, PT_R13, \docfi
	cfi_st	t2, PT_R14, \docfi
	cfi_st	t3, PT_R15, \docfi
	cfi_st	t4, PT_R16, \docfi
	cfi_st	t5, PT_R17, \docfi
	cfi_st	t6, PT_R18, \docfi
	cfi_st	t7, PT_R19, \docfi
	cfi_st	t8, PT_R20, \docfi
	.endm

	.macro	SAVE_STATIC docfi=0
	cfi_st	s0, PT_R23, \docfi
	cfi_st	s1, PT_R24, \docfi
	cfi_st	s2, PT_R25, \docfi
	cfi_st	s3, PT_R26, \docfi
	cfi_st	s4, PT_R27, \docfi
	cfi_st	s5, PT_R28, \docfi
	cfi_st	s6, PT_R29, \docfi
	cfi_st	s7, PT_R30, \docfi
	cfi_st	s8, PT_R31, \docfi
	.endm

/*
 * get_saved_sp returns the SP for the current CPU by looking in the
 * kernelsp array for it. It stores the current sp in t0 and loads the
 * new value in sp.
 */
	.macro	get_saved_sp docfi=0
	la_abs	  t1, kernelsp
#ifdef CONFIG_SMP
	csrrd	  t0, PERCPU_BASE_KS
	LONG_ADD  t1, t1, t0
#endif
	move	  t0, sp
	.if \docfi
	.cfi_register sp, t0
	.endif
	LONG_L	  sp, t1, 0
	.endm

	.macro	set_saved_sp stackp temp temp2
	la.pcrel  \temp, kernelsp
#ifdef CONFIG_SMP
	LONG_ADD  \temp, \temp, u0
#endif
	LONG_S	  \stackp, \temp, 0
	.endm

	.macro	SAVE_SOME docfi=0
	csrrd	t1, LOONGARCH_CSR_PRMD
	andi	t1, t1, 0x3	/* extract pplv bit */
	move	t0, sp
	beqz	t1, 8f
	/* Called from user mode, new stack. */
	get_saved_sp docfi=\docfi
8:
	PTR_ADDI sp, sp, -PT_SIZE
	.if \docfi
	.cfi_def_cfa sp, 0
	.endif
	cfi_st	t0, PT_R3, \docfi
	cfi_rel_offset  sp, PT_R3, \docfi
	LONG_S	zero, sp, PT_R0
	csrrd	t0, LOONGARCH_CSR_PRMD
	LONG_S	t0, sp, PT_PRMD
	csrrd	t0, LOONGARCH_CSR_CRMD
	LONG_S	t0, sp, PT_CRMD
	csrrd	t0, LOONGARCH_CSR_EUEN
	LONG_S  t0, sp, PT_EUEN
	csrrd	t0, LOONGARCH_CSR_ECFG
	LONG_S	t0, sp, PT_ECFG
	csrrd	t0, LOONGARCH_CSR_ESTAT
	PTR_S	t0, sp, PT_ESTAT
	cfi_st	ra, PT_R1, \docfi
	cfi_st	a0, PT_R4, \docfi
	cfi_st	a1, PT_R5, \docfi
	cfi_st	a2, PT_R6, \docfi
	cfi_st	a3, PT_R7, \docfi
	cfi_st	a4, PT_R8, \docfi
	cfi_st	a5, PT_R9, \docfi
	cfi_st	a6, PT_R10, \docfi
	cfi_st	a7, PT_R11, \docfi
	csrrd	ra, LOONGARCH_CSR_ERA
	LONG_S	ra, sp, PT_ERA
	.if \docfi
	.cfi_rel_offset ra, PT_ERA
	.endif
	cfi_st	tp, PT_R2, \docfi
	cfi_st	fp, PT_R22, \docfi

	/* Set thread_info if we're coming from user mode */
	csrrd	t0, LOONGARCH_CSR_PRMD
	andi	t0, t0, 0x3	/* extract pplv bit */
	beqz	t0, 9f

	li.d	tp, ~_THREAD_MASK
	and	tp, tp, sp
	cfi_st  u0, PT_R21, \docfi
	csrrd	u0, PERCPU_BASE_KS
9:
	.endm

	.macro	SAVE_ALL docfi=0
	SAVE_SOME \docfi
	SAVE_TEMP \docfi
	SAVE_STATIC \docfi
	.endm

	.macro	RESTORE_TEMP docfi=0
	cfi_ld	t0, PT_R12, \docfi
	cfi_ld	t1, PT_R13, \docfi
	cfi_ld	t2, PT_R14, \docfi
	cfi_ld	t3, PT_R15, \docfi
	cfi_ld	t4, PT_R16, \docfi
	cfi_ld	t5, PT_R17, \docfi
	cfi_ld	t6, PT_R18, \docfi
	cfi_ld	t7, PT_R19, \docfi
	cfi_ld	t8, PT_R20, \docfi
	.endm

	.macro	RESTORE_STATIC docfi=0
	cfi_ld	s0, PT_R23, \docfi
	cfi_ld	s1, PT_R24, \docfi
	cfi_ld	s2, PT_R25, \docfi
	cfi_ld	s3, PT_R26, \docfi
	cfi_ld	s4, PT_R27, \docfi
	cfi_ld	s5, PT_R28, \docfi
	cfi_ld	s6, PT_R29, \docfi
	cfi_ld	s7, PT_R30, \docfi
	cfi_ld	s8, PT_R31, \docfi
	.endm

	.macro	RESTORE_SOME docfi=0
	LONG_L	a0, sp, PT_PRMD
	andi    a0, a0, 0x3	/* extract pplv bit */
	beqz    a0, 8f
	cfi_ld  u0, PT_R21, \docfi
8:
	LONG_L	a0, sp, PT_ERA
	csrwr	a0, LOONGARCH_CSR_ERA
	LONG_L	a0, sp, PT_PRMD
	csrwr	a0, LOONGARCH_CSR_PRMD
	cfi_ld	ra, PT_R1, \docfi
	cfi_ld	a0, PT_R4, \docfi
	cfi_ld	a1, PT_R5, \docfi
	cfi_ld	a2, PT_R6, \docfi
	cfi_ld	a3, PT_R7, \docfi
	cfi_ld	a4, PT_R8, \docfi
	cfi_ld	a5, PT_R9, \docfi
	cfi_ld	a6, PT_R10, \docfi
	cfi_ld	a7, PT_R11, \docfi
	cfi_ld	tp, PT_R2, \docfi
	cfi_ld	fp, PT_R22, \docfi
	.endm

	.macro	RESTORE_SP_AND_RET docfi=0
	cfi_ld	sp, PT_R3, \docfi
	ertn
	.endm

	.macro	RESTORE_ALL_AND_RET docfi=0
	RESTORE_STATIC \docfi
	RESTORE_TEMP \docfi
	RESTORE_SOME \docfi
	RESTORE_SP_AND_RET \docfi
	.endm

#endif /* _ASM_STACKFRAME_H */
