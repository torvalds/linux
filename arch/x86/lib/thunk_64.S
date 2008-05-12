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
	#include <asm/rwlock.h>
		
	/* rdi:	arg1 ... normal C conventions. rax is saved/restored. */ 	
	.macro thunk name,func
	.globl \name
\name:	
	CFI_STARTPROC
	SAVE_ARGS
	call \func
	jmp  restore
	CFI_ENDPROC
	.endm

	/* rdi:	arg1 ... normal C conventions. rax is passed from C. */ 	
	.macro thunk_retrax name,func
	.globl \name
\name:	
	CFI_STARTPROC
	SAVE_ARGS
	call \func
	jmp  restore_norax
	CFI_ENDPROC
	.endm
	

	.section .sched.text, "ax"
#ifdef CONFIG_RWSEM_XCHGADD_ALGORITHM
	thunk rwsem_down_read_failed_thunk,rwsem_down_read_failed
	thunk rwsem_down_write_failed_thunk,rwsem_down_write_failed
	thunk rwsem_wake_thunk,rwsem_wake
	thunk rwsem_downgrade_thunk,rwsem_downgrade_wake
#endif	
	
#ifdef CONFIG_TRACE_IRQFLAGS
	/* put return address in rdi (arg1) */
	.macro thunk_ra name,func
	.globl \name
\name:
	CFI_STARTPROC
	SAVE_ARGS
	/* SAVE_ARGS pushs 9 elements */
	/* the next element would be the rip */
	movq 9*8(%rsp), %rdi
	call \func
	jmp  restore
	CFI_ENDPROC
	.endm

	thunk_ra trace_hardirqs_on_thunk,trace_hardirqs_on_caller
	thunk_ra trace_hardirqs_off_thunk,trace_hardirqs_off_caller
#endif

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	thunk lockdep_sys_exit_thunk,lockdep_sys_exit
#endif
	
	/* SAVE_ARGS below is used only for the .cfi directives it contains. */
	CFI_STARTPROC
	SAVE_ARGS
restore:
	RESTORE_ARGS
	ret	
	CFI_ENDPROC
	
	CFI_STARTPROC
	SAVE_ARGS
restore_norax:	
	RESTORE_ARGS 1
	ret
	CFI_ENDPROC
