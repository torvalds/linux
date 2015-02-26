/*
 * Save registers before calling assembly functions. This avoids
 * disturbance of register allocation in some inline assembly constructs.
 * Copyright 2001,2002 by Andi Kleen, SuSE Labs.
 * Added trace_hardirqs callers - Copyright 2007 Steven Rostedt, Red Hat, Inc.
 * Subject to the GNU public license, v.2. No warranty of any kind.
 */
#include <linux/linkage.h>
#include <asm/dwarf2.h>
#include <asm/calling.h>
#include <asm/asm.h>

	/* rdi:	arg1 ... normal C conventions. rax is saved/restored. */
	.macro THUNK name, func, put_ret_addr_in_rdi=0
	.globl \name
\name:
	CFI_STARTPROC

	/* this one pushes 9 elems, the next one would be %rIP */
	pushq_cfi %rdi
	CFI_REL_OFFSET rdi, 0
	pushq_cfi %rsi
	CFI_REL_OFFSET rsi, 0
	pushq_cfi %rdx
	CFI_REL_OFFSET rdx, 0
	pushq_cfi %rcx
	CFI_REL_OFFSET rcx, 0
	pushq_cfi %rax
	CFI_REL_OFFSET rax, 0
	pushq_cfi %r8
	CFI_REL_OFFSET r8, 0
	pushq_cfi %r9
	CFI_REL_OFFSET r9, 0
	pushq_cfi %r10
	CFI_REL_OFFSET r10, 0
	pushq_cfi %r11
	CFI_REL_OFFSET r11, 0

	.if \put_ret_addr_in_rdi
	/* 9*8(%rsp) is return addr on stack */
	movq_cfi_restore 9*8, rdi
	.endif

	call \func
	jmp  restore
	CFI_ENDPROC
	_ASM_NOKPROBE(\name)
	.endm

#ifdef CONFIG_TRACE_IRQFLAGS
	THUNK trace_hardirqs_on_thunk,trace_hardirqs_on_caller,1
	THUNK trace_hardirqs_off_thunk,trace_hardirqs_off_caller,1
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	THUNK lockdep_sys_exit_thunk,lockdep_sys_exit
#endif

#ifdef CONFIG_PREEMPT
	THUNK ___preempt_schedule, preempt_schedule
#ifdef CONFIG_CONTEXT_TRACKING
	THUNK ___preempt_schedule_context, preempt_schedule_context
#endif
#endif

#if defined(CONFIG_TRACE_IRQFLAGS) \
 || defined(CONFIG_DEBUG_LOCK_ALLOC) \
 || defined(CONFIG_PREEMPT)
	CFI_STARTPROC
	CFI_ADJUST_CFA_OFFSET 9*8
restore:
	popq_cfi %r11
	CFI_RESTORE r11
	popq_cfi %r10
	CFI_RESTORE r10
	popq_cfi %r9
	CFI_RESTORE r9
	popq_cfi %r8
	CFI_RESTORE r8
	popq_cfi %rax
	CFI_RESTORE rax
	popq_cfi %rcx
	CFI_RESTORE rcx
	popq_cfi %rdx
	CFI_RESTORE rdx
	popq_cfi %rsi
	CFI_RESTORE rsi
	popq_cfi %rdi
	CFI_RESTORE rdi
	ret
	CFI_ENDPROC
	_ASM_NOKPROBE(restore)
#endif
