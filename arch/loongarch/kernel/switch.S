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
 *			    struct thread_info *next_ti, void *sched_ra, void *sched_cfa)
 */
	.align	5
SYM_FUNC_START(__switch_to)
#ifdef CONFIG_32BIT
	PTR_ADDI	a0, a0, TASK_STRUCT_OFFSET
	PTR_ADDI	a1, a1, TASK_STRUCT_OFFSET
#endif
	csrrd		t1, LOONGARCH_CSR_PRMD
	LONG_SPTR	t1, a0, (THREAD_CSRPRMD - TASK_STRUCT_OFFSET)

	cpu_save_nonscratch a0
	LONG_SPTR	a3, a0, (THREAD_SCHED_RA - TASK_STRUCT_OFFSET)
	LONG_SPTR	a4, a0, (THREAD_SCHED_CFA - TASK_STRUCT_OFFSET)

#if defined(CONFIG_STACKPROTECTOR) && !defined(CONFIG_SMP)
	la		t7, __stack_chk_guard
	LONG_LPTR	t8, a1, (TASK_STACK_CANARY - TASK_STRUCT_OFFSET)
	LONG_SPTR	t8, t7, 0
#endif

	move	tp, a2
	cpu_restore_nonscratch a1

	li.w		t0, _THREAD_SIZE
	PTR_ADD		t0, t0, tp
	set_saved_sp	t0, t1, t2

	LONG_LPTR	t1, a1, (THREAD_CSRPRMD - TASK_STRUCT_OFFSET)
	csrwr		t1, LOONGARCH_CSR_PRMD

#ifdef CONFIG_32BIT
	PTR_ADDI	a0, a0, -TASK_STRUCT_OFFSET
#endif
	jr	ra
SYM_FUNC_END(__switch_to)
