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
#include <asm/asm-offsets.h>

#ifdef CONFIG_FRAME_POINTER
# define MCOUNT_FRAME			1	/* using frame = true  */
#else
# define MCOUNT_FRAME			0	/* using frame = false */
#endif

SYM_FUNC_START(__fentry__)
	RET
SYM_FUNC_END(__fentry__)
EXPORT_SYMBOL(__fentry__)

SYM_CODE_START(ftrace_caller)

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
SYM_INNER_LABEL_ALIGN(ftrace_stub, SYM_L_WEAK)
	RET
SYM_CODE_END(ftrace_caller)

SYM_CODE_START(ftrace_regs_caller)
	/*
	 * We're here from an mcount/fentry CALL, and the stack frame looks like:
	 *
	 *  <previous context>
	 *  RET-IP
	 *
	 * The purpose of this function is to call out in an emulated INT3
	 * environment with a stack frame like:
	 *
	 *  <previous context>
	 *  gap / RET-IP
	 *  gap
	 *  gap
	 *  gap
	 *  pt_regs
	 *
	 * We do _NOT_ restore: ss, flags, cs, gs, fs, es, ds
	 */
	subl	$3*4, %esp	# RET-IP + 3 gaps
	pushl	%ss		# ss
	pushl	%esp		# points at ss
	addl	$5*4, (%esp)	#   make it point at <previous context>
	pushfl			# flags
	pushl	$__KERNEL_CS	# cs
	pushl	7*4(%esp)	# ip <- RET-IP
	pushl	$0		# orig_eax

	pushl	%gs
	pushl	%fs
	pushl	%es
	pushl	%ds

	pushl	%eax
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%edx
	pushl	%ecx
	pushl	%ebx

	ENCODE_FRAME_POINTER

	movl	PT_EIP(%esp), %eax	# 1st argument: IP
	subl	$MCOUNT_INSN_SIZE, %eax
	movl	21*4(%esp), %edx	# 2nd argument: parent ip
	movl	function_trace_op, %ecx	# 3rd argument: ftrace_pos
	pushl	%esp			# 4th argument: pt_regs

SYM_INNER_LABEL(ftrace_regs_call, SYM_L_GLOBAL)
	call	ftrace_stub

	addl	$4, %esp		# skip 4th argument

	/* place IP below the new SP */
	movl	PT_OLDESP(%esp), %eax
	movl	PT_EIP(%esp), %ecx
	movl	%ecx, -4(%eax)

	/* place EAX below that */
	movl	PT_EAX(%esp), %ecx
	movl	%ecx, -8(%eax)

	popl	%ebx
	popl	%ecx
	popl	%edx
	popl	%esi
	popl	%edi
	popl	%ebp

	lea	-8(%eax), %esp
	popl	%eax

	jmp	.Lftrace_ret
SYM_CODE_END(ftrace_regs_caller)

SYM_FUNC_START(ftrace_stub_direct_tramp)
	CALL_DEPTH_ACCOUNT
	RET
SYM_FUNC_END(ftrace_stub_direct_tramp)

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
SYM_CODE_START(ftrace_graph_caller)
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
	RET
SYM_CODE_END(ftrace_graph_caller)

.globl return_to_handler
return_to_handler:
	pushl	$0
	pushl	%edx
	pushl	%eax
	movl	%esp, %eax
	call	ftrace_return_to_handler
	movl	%eax, %ecx
	popl	%eax
	popl	%edx
	addl	$4, %esp		# skip ebp
	JMP_NOSPEC ecx
#endif
