/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/asm-offsets.h>
#include <asm/loongarch.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/thread_info.h>

/*
 * task_struct *__switch_to(task_struct *prev, task_struct *next,
 *			    struct thread_info *next_ti)
 */
	.align	5
SYM_FUNC_START(__switch_to)
	csrrd	t1, LOONGARCH_CSR_PRMD
	stptr.d	t1, a0, THREAD_CSRPRMD

	cpu_save_nonscratch a0
	stptr.d	ra, a0, THREAD_REG01
	stptr.d a3, a0, THREAD_SCHED_RA
	stptr.d a4, a0, THREAD_SCHED_CFA
	move	tp, a2
	cpu_restore_nonscratch a1

	li.w		t0, _THREAD_SIZE
	PTR_ADD		t0, t0, tp
	set_saved_sp	t0, t1, t2

	ldptr.d	t1, a1, THREAD_CSRPRMD
	csrwr	t1, LOONGARCH_CSR_PRMD

	jr	ra
SYM_FUNC_END(__switch_to)
