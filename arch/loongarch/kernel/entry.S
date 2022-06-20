/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1994 - 2000, 2001, 2003 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */

#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/loongarch.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/thread_info.h>

	.text
	.cfi_sections	.debug_frame
	.align	5
SYM_FUNC_START(handle_syscall)
	csrrd	t0, PERCPU_BASE_KS
	la.abs	t1, kernelsp
	add.d	t1, t1, t0
	move	t2, sp
	ld.d	sp, t1, 0

	addi.d	sp, sp, -PT_SIZE
	cfi_st	t2, PT_R3
	cfi_rel_offset  sp, PT_R3
	st.d	zero, sp, PT_R0
	csrrd	t2, LOONGARCH_CSR_PRMD
	st.d	t2, sp, PT_PRMD
	csrrd	t2, LOONGARCH_CSR_CRMD
	st.d	t2, sp, PT_CRMD
	csrrd	t2, LOONGARCH_CSR_EUEN
	st.d	t2, sp, PT_EUEN
	csrrd	t2, LOONGARCH_CSR_ECFG
	st.d	t2, sp, PT_ECFG
	csrrd	t2, LOONGARCH_CSR_ESTAT
	st.d	t2, sp, PT_ESTAT
	cfi_st	ra, PT_R1
	cfi_st	a0, PT_R4
	cfi_st	a1, PT_R5
	cfi_st	a2, PT_R6
	cfi_st	a3, PT_R7
	cfi_st	a4, PT_R8
	cfi_st	a5, PT_R9
	cfi_st	a6, PT_R10
	cfi_st	a7, PT_R11
	csrrd	ra, LOONGARCH_CSR_ERA
	st.d	ra, sp, PT_ERA
	cfi_rel_offset ra, PT_ERA

	cfi_st	tp, PT_R2
	cfi_st	u0, PT_R21
	cfi_st	fp, PT_R22

	SAVE_STATIC

	move	u0, t0
	li.d	tp, ~_THREAD_MASK
	and	tp, tp, sp

	move	a0, sp
	bl	do_syscall

	RESTORE_ALL_AND_RET
SYM_FUNC_END(handle_syscall)

SYM_CODE_START(ret_from_fork)
	bl	schedule_tail		# a0 = struct task_struct *prev
	move	a0, sp
	bl 	syscall_exit_to_user_mode
	RESTORE_STATIC
	RESTORE_SOME
	RESTORE_SP_AND_RET
SYM_CODE_END(ret_from_fork)

SYM_CODE_START(ret_from_kernel_thread)
	bl	schedule_tail		# a0 = struct task_struct *prev
	move	a0, s1
	jirl	ra, s0, 0
	move	a0, sp
	bl	syscall_exit_to_user_mode
	RESTORE_STATIC
	RESTORE_SOME
	RESTORE_SP_AND_RET
SYM_CODE_END(ret_from_kernel_thread)
