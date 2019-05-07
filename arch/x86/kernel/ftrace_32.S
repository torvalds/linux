/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 2017  Steven Rostedt, VMware Inc.
 */

#include <linux/linkage.h>
#include <asm/page_types.h>
#include <asm/segment.h>
#include <asm/export.h>
#include <asm/ftrace.h>
#include <asm/nospec-branch.h>
#include <asm/frame.h>

# define function_hook	__fentry__
EXPORT_SYMBOL(__fentry__)

#ifdef CONFIG_FRAME_POINTER
# define MCOUNT_FRAME			1	/* using frame = true  */
#else
# define MCOUNT_FRAME			0	/* using frame = false */
#endif

ENTRY(function_hook)
	ret
END(function_hook)

ENTRY(ftrace_caller)

#ifdef CONFIG_FRAME_POINTER
	/*
	 * Frame pointers are of ip followed by bp.
	 * Since fentry is an immediate jump, we are left with
	 * parent-ip, function-ip. We need to add a frame with
	 * parent-ip followed by ebp.
	 */
	pushl	4(%esp)				/* parent ip */
	pushl	%ebp
	movl	%esp, %ebp
	pushl	2*4(%esp)			/* function ip */

	/* For mcount, the function ip is directly above */
	pushl	%ebp
	movl	%esp, %ebp
#endif
	pushl	%eax
	pushl	%ecx
	pushl	%edx
	pushl	$0				/* Pass NULL as regs pointer */

#ifdef CONFIG_FRAME_POINTER
	/* Load parent ebp into edx */
	movl	4*4(%esp), %edx
#else
	/* There's no frame pointer, load the appropriate stack addr instead */
	lea	4*4(%esp), %edx
#endif

	movl	(MCOUNT_FRAME+4)*4(%esp), %eax	/* load the rip */
	/* Get the parent ip */
	movl	4(%edx), %edx			/* edx has ebp */

	movl	function_trace_op, %ecx
	subl	$MCOUNT_INSN_SIZE, %eax

.globl ftrace_call
ftrace_call:
	call	ftrace_stub

	addl	$4, %esp			/* skip NULL pointer */
	popl	%edx
	popl	%ecx
	popl	%eax
#ifdef CONFIG_FRAME_POINTER
	popl	%ebp
	addl	$4,%esp				/* skip function ip */
	popl	%ebp				/* this is the orig bp */
	addl	$4, %esp			/* skip parent ip */
#endif
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

	ENCODE_FRAME_POINTER

	movl	12*4(%esp), %eax		/* Load ip (1st parameter) */
	subl	$MCOUNT_INSN_SIZE, %eax		/* Adjust ip */
	movl	15*4(%esp), %edx		/* Load parent ip (2nd parameter) */
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

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
	pushl	%eax
	pushl	%ecx
	pushl	%edx
	movl	3*4(%esp), %eax
	/* Even with frame pointers, fentry doesn't have one here */
	lea	4*4(%esp), %edx
	movl	$0, %ecx
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
	movl	$0, %eax
	call	ftrace_return_to_handler
	movl	%eax, %ecx
	popl	%edx
	popl	%eax
	JMP_NOSPEC %ecx
#endif
