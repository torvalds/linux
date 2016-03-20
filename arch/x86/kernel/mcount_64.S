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

/* All cases save the original rbp (8 bytes) */
#ifdef CONFIG_FRAME_POINTER
# ifdef CC_USING_FENTRY
/* Save parent and function stack frames (rip and rbp) */
#  define MCOUNT_FRAME_SIZE	(8+16*2)
# else
/* Save just function stack frame (rip and rbp) */
#  define MCOUNT_FRAME_SIZE	(8+16)
# endif
#else
/* No need to save a stack frame */
# define MCOUNT_FRAME_SIZE	8
#endif /* CONFIG_FRAME_POINTER */

/* Size of stack used to save mcount regs in save_mcount_regs */
#define MCOUNT_REG_SIZE		(SS+8 + MCOUNT_FRAME_SIZE)

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

/*
 * @added: the amount of stack added before calling this
 *
 * After this is called, the following registers contain:
 *
 *  %rdi - holds the address that called the trampoline
 *  %rsi - holds the parent function (traced function's return address)
 *  %rdx - holds the original %rbp
 */
.macro save_mcount_regs added=0

	/* Always save the original rbp */
	pushq %rbp

#ifdef CONFIG_FRAME_POINTER
	/*
	 * Stack traces will stop at the ftrace trampoline if the frame pointer
	 * is not set up properly. If fentry is used, we need to save a frame
	 * pointer for the parent as well as the function traced, because the
	 * fentry is called before the stack frame is set up, where as mcount
	 * is called afterward.
	 */
#ifdef CC_USING_FENTRY
	/* Save the parent pointer (skip orig rbp and our return address) */
	pushq \added+8*2(%rsp)
	pushq %rbp
	movq %rsp, %rbp
	/* Save the return address (now skip orig rbp, rbp and parent) */
	pushq \added+8*3(%rsp)
#else
	/* Can't assume that rip is before this (unless added was zero) */
	pushq \added+8(%rsp)
#endif
	pushq %rbp
	movq %rsp, %rbp
#endif /* CONFIG_FRAME_POINTER */

	/*
	 * We add enough stack to save all regs.
	 */
	subq $(MCOUNT_REG_SIZE - MCOUNT_FRAME_SIZE), %rsp
	movq %rax, RAX(%rsp)
	movq %rcx, RCX(%rsp)
	movq %rdx, RDX(%rsp)
	movq %rsi, RSI(%rsp)
	movq %rdi, RDI(%rsp)
	movq %r8, R8(%rsp)
	movq %r9, R9(%rsp)
	/*
	 * Save the original RBP. Even though the mcount ABI does not
	 * require this, it helps out callers.
	 */
	movq MCOUNT_REG_SIZE-8(%rsp), %rdx
	movq %rdx, RBP(%rsp)

	/* Copy the parent address into %rsi (second parameter) */
#ifdef CC_USING_FENTRY
	movq MCOUNT_REG_SIZE+8+\added(%rsp), %rsi
#else
	/* %rdx contains original %rbp */
	movq 8(%rdx), %rsi
#endif

	 /* Move RIP to its proper location */
	movq MCOUNT_REG_SIZE+\added(%rsp), %rdi
	movq %rdi, RIP(%rsp)

	/*
	 * Now %rdi (the first parameter) has the return address of
	 * where ftrace_call returns. But the callbacks expect the
	 * address of the call itself.
	 */
	subq $MCOUNT_INSN_SIZE, %rdi
	.endm

.macro restore_mcount_regs
	movq R9(%rsp), %r9
	movq R8(%rsp), %r8
	movq RDI(%rsp), %rdi
	movq RSI(%rsp), %rsi
	movq RDX(%rsp), %rdx
	movq RCX(%rsp), %rcx
	movq RAX(%rsp), %rax

	/* ftrace_regs_caller can modify %rbp */
	movq RBP(%rsp), %rbp

	addq $MCOUNT_REG_SIZE, %rsp

	.endm

#ifdef CONFIG_DYNAMIC_FTRACE

ENTRY(function_hook)
	retq
END(function_hook)

ENTRY(ftrace_caller)
	/* save_mcount_regs fills in first two parameters */
	save_mcount_regs

GLOBAL(ftrace_caller_op_ptr)
	/* Load the ftrace_ops into the 3rd parameter */
	movq function_trace_op(%rip), %rdx

	/* regs go into 4th parameter (but make it NULL) */
	movq $0, %rcx

GLOBAL(ftrace_call)
	call ftrace_stub

	restore_mcount_regs

	/*
	 * The copied trampoline must call ftrace_epilogue as it
	 * still may need to call the function graph tracer.
	 *
	 * The code up to this label is copied into trampolines so
	 * think twice before adding any new code or changing the
	 * layout here.
	 */
GLOBAL(ftrace_epilogue)

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
GLOBAL(ftrace_graph_call)
	jmp ftrace_stub
#endif

GLOBAL(ftrace_stub)
	retq
END(ftrace_caller)

ENTRY(ftrace_regs_caller)
	/* Save the current flags before any operations that can change them */
	pushfq

	/* added 8 bytes to save flags */
	save_mcount_regs 8
	/* save_mcount_regs fills in first two parameters */

GLOBAL(ftrace_regs_caller_op_ptr)
	/* Load the ftrace_ops into the 3rd parameter */
	movq function_trace_op(%rip), %rdx

	/* Save the rest of pt_regs */
	movq %r15, R15(%rsp)
	movq %r14, R14(%rsp)
	movq %r13, R13(%rsp)
	movq %r12, R12(%rsp)
	movq %r11, R11(%rsp)
	movq %r10, R10(%rsp)
	movq %rbx, RBX(%rsp)
	/* Copy saved flags */
	movq MCOUNT_REG_SIZE(%rsp), %rcx
	movq %rcx, EFLAGS(%rsp)
	/* Kernel segments */
	movq $__KERNEL_DS, %rcx
	movq %rcx, SS(%rsp)
	movq $__KERNEL_CS, %rcx
	movq %rcx, CS(%rsp)
	/* Stack - skipping return address and flags */
	leaq MCOUNT_REG_SIZE+8*2(%rsp), %rcx
	movq %rcx, RSP(%rsp)

	/* regs go into 4th parameter */
	leaq (%rsp), %rcx

GLOBAL(ftrace_regs_call)
	call ftrace_stub

	/* Copy flags back to SS, to restore them */
	movq EFLAGS(%rsp), %rax
	movq %rax, MCOUNT_REG_SIZE(%rsp)

	/* Handlers can change the RIP */
	movq RIP(%rsp), %rax
	movq %rax, MCOUNT_REG_SIZE+8(%rsp)

	/* restore the rest of pt_regs */
	movq R15(%rsp), %r15
	movq R14(%rsp), %r14
	movq R13(%rsp), %r13
	movq R12(%rsp), %r12
	movq R10(%rsp), %r10
	movq RBX(%rsp), %rbx

	restore_mcount_regs

	/* Restore flags */
	popfq

	/*
	 * As this jmp to ftrace_epilogue can be a short jump
	 * it must not be copied into the trampoline.
	 * The trampoline will add the code to jump
	 * to the return.
	 */
GLOBAL(ftrace_regs_caller_end)

	jmp ftrace_epilogue

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
	/* save_mcount_regs fills in first two parameters */
	save_mcount_regs

	/*
	 * When DYNAMIC_FTRACE is not defined, ARCH_SUPPORTS_FTRACE_OPS is not
	 * set (see include/asm/ftrace.h and include/linux/ftrace.h).  Only the
	 * ip and parent ip are used and the list function is called when
	 * function tracing is enabled.
	 */
	call   *ftrace_trace_function

	restore_mcount_regs

	jmp fgraph_trace
END(function_hook)
#endif /* CONFIG_DYNAMIC_FTRACE */
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
ENTRY(ftrace_graph_caller)
	/* Saves rbp into %rdx and fills first parameter  */
	save_mcount_regs

#ifdef CC_USING_FENTRY
	leaq MCOUNT_REG_SIZE+8(%rsp), %rsi
	movq $0, %rdx	/* No framepointers needed */
#else
	/* Save address of the return address of traced function */
	leaq 8(%rdx), %rsi
	/* ftrace does sanity checks against frame pointers */
	movq (%rdx), %rdx
#endif
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
