/*
 *  linux/arch/x86_64/mcount_64.S
 *
 *  Copyright (C) 2014  Steven Rostedt, Red Hat Inc
 */

#include <linux/linkage.h>
#include <asm/ptrace.h>
#include <asm/ftrace.h>


	.code64
	.section .entry.text, "ax"


#ifdef CONFIG_FUNCTION_TRACER

#ifdef CC_USING_FENTRY
# define function_hook	__fentry__
#else
# define function_hook	mcount
#endif

#ifdef CONFIG_DYNAMIC_FTRACE

ENTRY(function_hook)
	retq
END(function_hook)

/* skip is set if stack has been adjusted */
.macro ftrace_caller_setup skip=0
	MCOUNT_SAVE_FRAME \skip

	/* Load the ftrace_ops into the 3rd parameter */
	movq function_trace_op(%rip), %rdx

	/* Load ip into the first parameter */
	movq RIP(%rsp), %rdi
	subq $MCOUNT_INSN_SIZE, %rdi
	/* Load the parent_ip into the second parameter */
#ifdef CC_USING_FENTRY
	movq SS+16(%rsp), %rsi
#else
	movq 8(%rbp), %rsi
#endif
.endm

ENTRY(ftrace_caller)
	/* Check if tracing was disabled (quick check) */
	cmpl $0, function_trace_stop
	jne  ftrace_stub

	ftrace_caller_setup
	/* regs go into 4th parameter (but make it NULL) */
	movq $0, %rcx

GLOBAL(ftrace_call)
	call ftrace_stub

	MCOUNT_RESTORE_FRAME
ftrace_return:

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
GLOBAL(ftrace_graph_call)
	jmp ftrace_stub
#endif

GLOBAL(ftrace_stub)
	retq
END(ftrace_caller)

ENTRY(ftrace_regs_caller)
	/* Save the current flags before compare (in SS location)*/
	pushfq

	/* Check if tracing was disabled (quick check) */
	cmpl $0, function_trace_stop
	jne  ftrace_restore_flags

	/* skip=8 to skip flags saved in SS */
	ftrace_caller_setup 8

	/* Save the rest of pt_regs */
	movq %r15, R15(%rsp)
	movq %r14, R14(%rsp)
	movq %r13, R13(%rsp)
	movq %r12, R12(%rsp)
	movq %r11, R11(%rsp)
	movq %r10, R10(%rsp)
	movq %rbp, RBP(%rsp)
	movq %rbx, RBX(%rsp)
	/* Copy saved flags */
	movq SS(%rsp), %rcx
	movq %rcx, EFLAGS(%rsp)
	/* Kernel segments */
	movq $__KERNEL_DS, %rcx
	movq %rcx, SS(%rsp)
	movq $__KERNEL_CS, %rcx
	movq %rcx, CS(%rsp)
	/* Stack - skipping return address */
	leaq SS+16(%rsp), %rcx
	movq %rcx, RSP(%rsp)

	/* regs go into 4th parameter */
	leaq (%rsp), %rcx

GLOBAL(ftrace_regs_call)
	call ftrace_stub

	/* Copy flags back to SS, to restore them */
	movq EFLAGS(%rsp), %rax
	movq %rax, SS(%rsp)

	/* Handlers can change the RIP */
	movq RIP(%rsp), %rax
	movq %rax, SS+8(%rsp)

	/* restore the rest of pt_regs */
	movq R15(%rsp), %r15
	movq R14(%rsp), %r14
	movq R13(%rsp), %r13
	movq R12(%rsp), %r12
	movq R10(%rsp), %r10
	movq RBP(%rsp), %rbp
	movq RBX(%rsp), %rbx

	/* skip=8 to skip flags saved in SS */
	MCOUNT_RESTORE_FRAME 8

	/* Restore flags */
	popfq

	jmp ftrace_return
ftrace_restore_flags:
	popfq
	jmp  ftrace_stub

END(ftrace_regs_caller)


#else /* ! CONFIG_DYNAMIC_FTRACE */

ENTRY(function_hook)
	cmpl $0, function_trace_stop
	jne  ftrace_stub

	cmpq $ftrace_stub, ftrace_trace_function
	jnz trace

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	cmpq $ftrace_stub, ftrace_graph_return
	jnz ftrace_graph_caller

	cmpq $ftrace_graph_entry_stub, ftrace_graph_entry
	jnz ftrace_graph_caller
#endif

GLOBAL(ftrace_stub)
	retq

trace:
	MCOUNT_SAVE_FRAME

	movq RIP(%rsp), %rdi
#ifdef CC_USING_FENTRY
	movq SS+16(%rsp), %rsi
#else
	movq 8(%rbp), %rsi
#endif
	subq $MCOUNT_INSN_SIZE, %rdi

	call   *ftrace_trace_function

	MCOUNT_RESTORE_FRAME

	jmp ftrace_stub
END(function_hook)
#endif /* CONFIG_DYNAMIC_FTRACE */
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
	MCOUNT_SAVE_FRAME

	/* Check if tracing was disabled (quick check) */
	cmpl $0, function_trace_stop
	jne  fgraph_skip

#ifdef CC_USING_FENTRY
	leaq SS+16(%rsp), %rdi
	movq $0, %rdx	/* No framepointers needed */
#else
	leaq 8(%rbp), %rdi
	movq (%rbp), %rdx
#endif
	movq RIP(%rsp), %rsi
	subq $MCOUNT_INSN_SIZE, %rsi

	call	prepare_ftrace_return

fgraph_skip:
	MCOUNT_RESTORE_FRAME

	retq
END(ftrace_graph_caller)

GLOBAL(return_to_handler)
	subq  $24, %rsp

	/* Save the return values */
	movq %rax, (%rsp)
	movq %rdx, 8(%rsp)
	movq %rbp, %rdi

	call ftrace_return_to_handler

	movq %rax, %rdi
	movq 8(%rsp), %rdx
	movq (%rsp), %rax
	addq $24, %rsp
	jmp *%rdi
#endif
