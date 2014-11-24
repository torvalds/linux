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

/*
 * gcc -pg option adds a call to 'mcount' in most functions.
 * When -mfentry is used, the call is to 'fentry' and not 'mcount'
 * and is done before the function's stack frame is set up.
 * They both require a set of regs to be saved before calling
 * any C code and restored before returning back to the function.
 *
 * On boot up, all these calls are converted into nops. When tracing
 * is enabled, the call can jump to either ftrace_caller or
 * ftrace_regs_caller. Callbacks (tracing functions) that require
 * ftrace_regs_caller (like kprobes) need to have pt_regs passed to
 * it. For this reason, the size of the pt_regs structure will be
 * allocated on the stack and the required mcount registers will
 * be saved in the locations that pt_regs has them in.
 */

/* skip is set if the stack was already partially adjusted */
.macro save_mcount_regs skip=0
	 /*
	  * We add enough stack to save all regs.
	  */
	subq $(SS+8-\skip), %rsp
	movq %rax, RAX(%rsp)
	movq %rcx, RCX(%rsp)
	movq %rdx, RDX(%rsp)
	movq %rsi, RSI(%rsp)
	movq %rdi, RDI(%rsp)
	movq %r8, R8(%rsp)
	movq %r9, R9(%rsp)
	 /* Move RIP to its proper location */
	movq SS+8(%rsp), %rdi
	movq %rdi, RIP(%rsp)
	.endm

.macro restore_mcount_regs skip=0
	movq R9(%rsp), %r9
	movq R8(%rsp), %r8
	movq RDI(%rsp), %rdi
	movq RSI(%rsp), %rsi
	movq RDX(%rsp), %rdx
	movq RCX(%rsp), %rcx
	movq RAX(%rsp), %rax
	addq $(SS+8-\skip), %rsp
	.endm

/* skip is set if stack has been adjusted */
.macro ftrace_caller_setup trace_label skip=0
	save_mcount_regs \skip

	/* Save this location */
GLOBAL(\trace_label)
	/* Load the ftrace_ops into the 3rd parameter */
	movq function_trace_op(%rip), %rdx

	/* %rdi already has %rip from the save_mcount_regs macro */
	subq $MCOUNT_INSN_SIZE, %rdi
	/* Load the parent_ip into the second parameter */
#ifdef CC_USING_FENTRY
	movq SS+16(%rsp), %rsi
#else
	movq 8(%rbp), %rsi
#endif
.endm

#ifdef CONFIG_DYNAMIC_FTRACE

ENTRY(function_hook)
	retq
END(function_hook)

#ifdef CONFIG_FRAME_POINTER
/*
 * Stack traces will stop at the ftrace trampoline if the frame pointer
 * is not set up properly. If fentry is used, we need to save a frame
 * pointer for the parent as well as the function traced, because the
 * fentry is called before the stack frame is set up, where as mcount
 * is called afterward.
 */
.macro create_frame parent rip
#ifdef CC_USING_FENTRY
	pushq \parent
	pushq %rbp
	movq %rsp, %rbp
#endif
	pushq \rip
	pushq %rbp
	movq %rsp, %rbp
.endm

.macro restore_frame
#ifdef CC_USING_FENTRY
	addq $16, %rsp
#endif
	popq %rbp
	addq $8, %rsp
.endm
#else
.macro create_frame parent rip
.endm
.macro restore_frame
.endm
#endif /* CONFIG_FRAME_POINTER */

ENTRY(ftrace_caller)
	ftrace_caller_setup ftrace_caller_op_ptr
	/* regs go into 4th parameter (but make it NULL) */
	movq $0, %rcx

	create_frame %rsi, %rdi

GLOBAL(ftrace_call)
	call ftrace_stub

	restore_frame

	restore_mcount_regs

	/*
	 * The copied trampoline must call ftrace_return as it
	 * still may need to call the function graph tracer.
	 */
GLOBAL(ftrace_caller_end)

GLOBAL(ftrace_return)

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

	/* skip=8 to skip flags saved in SS */
	ftrace_caller_setup ftrace_regs_caller_op_ptr 8

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

	create_frame %rsi, %rdi

GLOBAL(ftrace_regs_call)
	call ftrace_stub

	restore_frame

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
	restore_mcount_regs 8

	/* Restore flags */
	popfq

	/*
	 * As this jmp to ftrace_return can be a short jump
	 * it must not be copied into the trampoline.
	 * The trampoline will add the code to jump
	 * to the return.
	 */
GLOBAL(ftrace_regs_caller_end)

	jmp ftrace_return

	popfq
	jmp  ftrace_stub

END(ftrace_regs_caller)


#else /* ! CONFIG_DYNAMIC_FTRACE */

ENTRY(function_hook)
	cmpq $ftrace_stub, ftrace_trace_function
	jnz trace

fgraph_trace:
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	cmpq $ftrace_stub, ftrace_graph_return
	jnz ftrace_graph_caller

	cmpq $ftrace_graph_entry_stub, ftrace_graph_entry
	jnz ftrace_graph_caller
#endif

GLOBAL(ftrace_stub)
	retq

trace:
	ftrace_caller_setup ftrace_caller_op_ptr

	call   *ftrace_trace_function

	restore_mcount_regs

	jmp fgraph_trace
END(function_hook)
#endif /* CONFIG_DYNAMIC_FTRACE */
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
	save_mcount_regs

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

	restore_mcount_regs

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
