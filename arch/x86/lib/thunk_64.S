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
	SAVE_ARGS

	.if \put_ret_addr_in_rdi
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

	/* SAVE_ARGS below is used only for the .cfi directives it contains. */
	CFI_STARTPROC
	SAVE_ARGS
restore:
	RESTORE_ARGS
	ret
	CFI_ENDPROC
	_ASM_NOKPROBE(restore)
