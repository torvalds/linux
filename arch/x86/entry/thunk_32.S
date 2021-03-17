/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Trampoline to trace irqs off. (otherwise CALLER_ADDR1 might crash)
 * Copyright 2008 by Steven Rostedt, Red Hat, Inc
 *  (inspired by Andi Kleen's thunk_64.S)
 */
	#include <linux/linkage.h>
	#include <asm/asm.h>
	#include <asm/export.h>

	/* put return address in eax (arg1) */
	.macro THUNK name, func, put_ret_addr_in_eax=0
SYM_CODE_START_NOALIGN(\name)
	pushl %eax
	pushl %ecx
	pushl %edx

	.if \put_ret_addr_in_eax
	/* Place EIP in the arg1 */
	movl 3*4(%esp), %eax
	.endif

	call \func
	popl %edx
	popl %ecx
	popl %eax
	ret
	_ASM_NOKPROBE(\name)
SYM_CODE_END(\name)
	.endm

#ifdef CONFIG_PREEMPTION
	THUNK preempt_schedule_thunk, preempt_schedule
	THUNK preempt_schedule_notrace_thunk, preempt_schedule_notrace
	EXPORT_SYMBOL(preempt_schedule_thunk)
	EXPORT_SYMBOL(preempt_schedule_notrace_thunk)
#endif

