/*
 *  Copyright (C) 2017  Steven Rostedt, VMware Inc.
 */

#include <linux/linkage.h>
#include <asm/page_types.h>
#include <asm/segment.h>
#include <asm/export.h>
#include <asm/ftrace.h>

#ifdef CONFIG_FUNCTION_TRACER
#ifdef CONFIG_DYNAMIC_FTRACE

ENTRY(mcount)
	ret
END(mcount)

ENTRY(ftrace_caller)

	pushl	%ebp
	movl	%esp, %ebp

	pushl	%eax
	pushl	%ecx
	pushl	%edx
	pushl	$0				/* Pass NULL as regs pointer */
	movl	5*4(%esp), %eax
	/* Copy original ebp into %edx */
	movl	4*4(%esp), %edx
	/* Get the parent ip */
	movl	0x4(%edx), %edx
	movl	function_trace_op, %ecx
	subl	$MCOUNT_INSN_SIZE, %eax

.globl ftrace_call
ftrace_call:
	call	ftrace_stub

	addl	$4, %esp			/* skip NULL pointer */
	popl	%edx
	popl	%ecx
	popl	%eax
	popl	%ebp
.Lftrace_ret:
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
.globl ftrace_graph_call
ftrace_graph_call:
	jmp	ftrace_stub
#endif

/* This is weak to keep gas from relaxing the jumps */
WEAK(ftrace_stub)
	ret
END(ftrace_caller)

ENTRY(ftrace_regs_caller)
	/*
	 * i386 does not save SS and ESP when coming from kernel.
	 * Instead, to get sp, &regs->sp is used (see ptrace.h).
	 * Unfortunately, that means eflags must be at the same location
	 * as the current return ip is. We move the return ip into the
	 * regs->ip location, and move flags into the return ip location.
	 */
	pushl	$__KERNEL_CS
	pushl	4(%esp)				/* Save the return ip */
	pushl	$0				/* Load 0 into orig_ax */
	pushl	%gs
	pushl	%fs
	pushl	%es
	pushl	%ds
	pushl	%eax

	/* Get flags and place them into the return ip slot */
	pushf
	popl	%eax
	movl	%eax, 8*4(%esp)

	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%edx
	pushl	%ecx
	pushl	%ebx

	movl	12*4(%esp), %eax		/* Load ip (1st parameter) */
	subl	$MCOUNT_INSN_SIZE, %eax		/* Adjust ip */
	movl	0x4(%ebp), %edx			/* Load parent ip (2nd parameter) */
	movl	function_trace_op, %ecx		/* Save ftrace_pos in 3rd parameter */
	pushl	%esp				/* Save pt_regs as 4th parameter */

GLOBAL(ftrace_regs_call)
	call	ftrace_stub

	addl	$4, %esp			/* Skip pt_regs */

	/* restore flags */
	push	14*4(%esp)
	popf

	/* Move return ip back to its original location */
	movl	12*4(%esp), %eax
	movl	%eax, 14*4(%esp)

	popl	%ebx
	popl	%ecx
	popl	%edx
	popl	%esi
	popl	%edi
	popl	%ebp
	popl	%eax
	popl	%ds
	popl	%es
	popl	%fs
	popl	%gs

	/* use lea to not affect flags */
	lea	3*4(%esp), %esp			/* Skip orig_ax, ip and cs */

	jmp	.Lftrace_ret
#else /* ! CONFIG_DYNAMIC_FTRACE */

ENTRY(mcount)
	cmpl	$__PAGE_OFFSET, %esp
	jb	ftrace_stub			/* Paging not enabled yet? */

	cmpl	$ftrace_stub, ftrace_trace_function
	jnz	.Ltrace
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	cmpl	$ftrace_stub, ftrace_graph_return
	jnz	ftrace_graph_caller

	cmpl	$ftrace_graph_entry_stub, ftrace_graph_entry
	jnz	ftrace_graph_caller
#endif
.globl ftrace_stub
ftrace_stub:
	ret

	/* taken from glibc */
.Ltrace:
	pushl	%eax
	pushl	%ecx
	pushl	%edx
	movl	0xc(%esp), %eax
	movl	0x4(%ebp), %edx
	subl	$MCOUNT_INSN_SIZE, %eax

	call	*ftrace_trace_function

	popl	%edx
	popl	%ecx
	popl	%eax
	jmp	ftrace_stub
END(mcount)
#endif /* CONFIG_DYNAMIC_FTRACE */
EXPORT_SYMBOL(mcount)
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
	pushl	%eax
	pushl	%ecx
	pushl	%edx
	movl	0xc(%esp), %eax
	lea	0x4(%ebp), %edx
	movl	(%ebp), %ecx
	subl	$MCOUNT_INSN_SIZE, %eax
	call	prepare_ftrace_return
	popl	%edx
	popl	%ecx
	popl	%eax
	ret
END(ftrace_graph_caller)

.globl return_to_handler
return_to_handler:
	pushl	%eax
	pushl	%edx
	movl	%ebp, %eax
	call	ftrace_return_to_handler
	movl	%eax, %ecx
	popl	%edx
	popl	%eax
	jmp	*%ecx
#endif
