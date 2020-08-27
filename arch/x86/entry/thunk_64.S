/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Save registers before calling assembly functions. This avoids
 * disturbance of register allocation in some inline assembly constructs.
 * Copyright 2001,2002 by Andi Kleen, SuSE Labs.
 */
#include <linux/linkage.h>
#include "calling.h"
#include <asm/asm.h>
#include <asm/export.h>

	/* rdi:	arg1 ... normal C conventions. rax is saved/restored. */
	.macro THUNK name, func, put_ret_addr_in_rdi=0
SYM_FUNC_START_NOALIGN(\name)
	pushq %rbp
	movq %rsp, %rbp

	pushq %rdi
	pushq %rsi
	pushq %rdx
	pushq %rcx
	pushq %rax
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11

	.if \put_ret_addr_in_rdi
	/* 8(%rbp) is return addr on stack */
	movq 8(%rbp), %rdi
	.endif

	call \func
	jmp  .L_restore
SYM_FUNC_END(\name)
	_ASM_NOKPROBE(\name)
	.endm

#ifdef CONFIG_PREEMPTION
	THUNK preempt_schedule_thunk, preempt_schedule
	THUNK preempt_schedule_notrace_thunk, preempt_schedule_notrace
	EXPORT_SYMBOL(preempt_schedule_thunk)
	EXPORT_SYMBOL(preempt_schedule_notrace_thunk)
#endif

#ifdef CONFIG_PREEMPTION
SYM_CODE_START_LOCAL_NOALIGN(.L_restore)
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rax
	popq %rcx
	popq %rdx
	popq %rsi
	popq %rdi
	popq %rbp
	ret
	_ASM_NOKPROBE(.L_restore)
SYM_CODE_END(.L_restore)
#endif
