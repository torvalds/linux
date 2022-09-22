/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 2014  Steven Rostedt, Red Hat Inc
 */

#include <linux/linkage.h>
#include <asm/ptrace.h>
#include <asm/ftrace.h>
#include <asm/export.h>
#include <asm/nospec-branch.h>
#include <asm/unwind_hints.h>
#include <asm/frame.h>

	.code64
	.section .text, "ax"

#ifdef CONFIG_FRAME_POINTER
/* Save parent and function stack frames (rip and rbp) */
#  define MCOUNT_FRAME_SIZE	(8+16*2)
#else
/* No need to save a stack frame */
# define MCOUNT_FRAME_SIZE	0
#endif /* CONFIG_FRAME_POINTER */

/* Size of stack used to save mcount regs in save_mcount_regs */
#define MCOUNT_REG_SIZE		(FRAME_SIZE + MCOUNT_FRAME_SIZE)

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

#ifdef CONFIG_FRAME_POINTER
	/* Save the original rbp */
	pushq %rbp

	/*
	 * Stack traces will stop at the ftrace trampoline if the frame pointer
	 * is not set up properly. If fentry is used, we need to save a frame
	 * pointer for the parent as well as the function traced, because the
	 * fentry is called before the stack frame is set up, where as mcount
	 * is called afterward.
	 */

	/* Save the parent pointer (skip orig rbp and our return address) */
	pushq \added+8*2(%rsp)
	pushq %rbp
	movq %rsp, %rbp
	/* Save the return address (now skip orig rbp, rbp and parent) */
	pushq \added+8*3(%rsp)
	pushq %rbp
	movq %rsp, %rbp
#endif /* CONFIG_FRAME_POINTER */

	/*
	 * We add enough stack to save all regs.
	 */
	subq $(FRAME_SIZE), %rsp
	movq %rax, RAX(%rsp)
	movq %rcx, RCX(%rsp)
	movq %rdx, RDX(%rsp)
	movq %rsi, RSI(%rsp)
	movq %rdi, RDI(%rsp)
	movq %r8, R8(%rsp)
	movq %r9, R9(%rsp)
	movq $0, ORIG_RAX(%rsp)
	/*
	 * Save the original RBP. Even though the mcount ABI does not
	 * require this, it helps out callers.
	 */
#ifdef CONFIG_FRAME_POINTER
	movq MCOUNT_REG_SIZE-8(%rsp), %rdx
#else
	movq %rbp, %rdx
#endif
	movq %rdx, RBP(%rsp)

	/* Copy the parent address into %rsi (second parameter) */
	movq MCOUNT_REG_SIZE+8+\added(%rsp), %rsi

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

.macro restore_mcount_regs save=0

	/* ftrace_regs_caller or frame pointers require this */
	movq RBP(%rsp), %rbp

	movq R9(%rsp), %r9
	movq R8(%rsp), %r8
	movq RDI(%rsp), %rdi
	movq RSI(%rsp), %rsi
	movq RDX(%rsp), %rdx
	movq RCX(%rsp), %rcx
	movq RAX(%rsp), %rax

	addq $MCOUNT_REG_SIZE-\save, %rsp

	.endm

#ifdef CONFIG_DYNAMIC_FTRACE

SYM_FUNC_START(__fentry__)
	RET
SYM_FUNC_END(__fentry__)
EXPORT_SYMBOL(__fentry__)

SYM_FUNC_START(ftrace_caller)
	/* save_mcount_regs fills in first two parameters */
	save_mcount_regs

SYM_INNER_LABEL(ftrace_caller_op_ptr, SYM_L_GLOBAL)
	/* Load the ftrace_ops into the 3rd parameter */
	movq function_trace_op(%rip), %rdx

	/* regs go into 4th parameter (but make it NULL) */
	movq $0, %rcx

SYM_INNER_LABEL(ftrace_call, SYM_L_GLOBAL)
	call ftrace_stub

	restore_mcount_regs

	/*
	 * The code up to this label is copied into trampolines so
	 * think twice before adding any new code or changing the
	 * layout here.
	 */
SYM_INNER_LABEL(ftrace_caller_end, SYM_L_GLOBAL)

	jmp ftrace_epilogue
SYM_FUNC_END(ftrace_caller);

SYM_FUNC_START(ftrace_epilogue)
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
SYM_INNER_LABEL(ftrace_graph_call, SYM_L_GLOBAL)
	jmp ftrace_stub
#endif

/*
 * This is weak to keep gas from relaxing the jumps.
 */
SYM_INNER_LABEL_ALIGN(ftrace_stub, SYM_L_WEAK)
	UNWIND_HINT_FUNC
	RET
SYM_FUNC_END(ftrace_epilogue)

SYM_FUNC_START(ftrace_regs_caller)
	/* Save the current flags before any operations that can change them */
	pushfq

	/* added 8 bytes to save flags */
	save_mcount_regs 8
	/* save_mcount_regs fills in first two parameters */

SYM_INNER_LABEL(ftrace_regs_caller_op_ptr, SYM_L_GLOBAL)
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

	ENCODE_FRAME_POINTER

	/* regs go into 4th parameter */
	leaq (%rsp), %rcx

SYM_INNER_LABEL(ftrace_regs_call, SYM_L_GLOBAL)
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

	movq ORIG_RAX(%rsp), %rax
	movq %rax, MCOUNT_REG_SIZE-8(%rsp)

	/*
	 * If ORIG_RAX is anything but zero, make this a call to that.
	 * See arch_ftrace_set_direct_caller().
	 */
	movq ORIG_RAX(%rsp), %rax
	testq	%rax, %rax
SYM_INNER_LABEL(ftrace_regs_caller_jmp, SYM_L_GLOBAL)
	jnz	1f

	restore_mcount_regs
	/* Restore flags */
	popfq

	/*
	 * As this jmp to ftrace_epilogue can be a short jump
	 * it must not be copied into the trampoline.
	 * The trampoline will add the code to jump
	 * to the return.
	 */
SYM_INNER_LABEL(ftrace_regs_caller_end, SYM_L_GLOBAL)
	jmp ftrace_epilogue

	/* Swap the flags with orig_rax */
1:	movq MCOUNT_REG_SIZE(%rsp), %rdi
	movq %rdi, MCOUNT_REG_SIZE-8(%rsp)
	movq %rax, MCOUNT_REG_SIZE(%rsp)

	restore_mcount_regs 8
	/* Restore flags */
	popfq
	UNWIND_HINT_FUNC
	jmp	ftrace_epilogue

SYM_FUNC_END(ftrace_regs_caller)


#else /* ! CONFIG_DYNAMIC_FTRACE */

SYM_FUNC_START(__fentry__)
	cmpq $ftrace_stub, ftrace_trace_function
	jnz trace

fgraph_trace:
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	cmpq $ftrace_stub, ftrace_graph_return
	jnz ftrace_graph_caller

	cmpq $ftrace_graph_entry_stub, ftrace_graph_entry
	jnz ftrace_graph_caller
#endif

SYM_INNER_LABEL(ftrace_stub, SYM_L_GLOBAL)
	RET

trace:
	/* save_mcount_regs fills in first two parameters */
	save_mcount_regs

	/*
	 * When DYNAMIC_FTRACE is not defined, ARCH_SUPPORTS_FTRACE_OPS is not
	 * set (see include/asm/ftrace.h and include/linux/ftrace.h).  Only the
	 * ip and parent ip are used and the list function is called when
	 * function tracing is enabled.
	 */
	movq ftrace_trace_function, %r8
	CALL_NOSPEC r8
	restore_mcount_regs

	jmp fgraph_trace
SYM_FUNC_END(__fentry__)
EXPORT_SYMBOL(__fentry__)
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
SYM_FUNC_START(ftrace_graph_caller)
	/* Saves rbp into %rdx and fills first parameter  */
	save_mcount_regs

	leaq MCOUNT_REG_SIZE+8(%rsp), %rsi
	movq $0, %rdx	/* No framepointers needed */
	call	prepare_ftrace_return

	restore_mcount_regs

	RET
SYM_FUNC_END(ftrace_graph_caller)

SYM_CODE_START(return_to_handler)
	UNWIND_HINT_EMPTY
	subq  $16, %rsp

	/* Save the return values */
	movq %rax, (%rsp)
	movq %rdx, 8(%rsp)
	movq %rbp, %rdi

	call ftrace_return_to_handler

	movq %rax, %rdi
	movq 8(%rsp), %rdx
	movq (%rsp), %rax

	addq $16, %rsp
	/*
	 * Jump back to the old return address. This cannot be JMP_NOSPEC rdi
	 * since IBT would demand that contain ENDBR, which simply isn't so for
	 * return addresses. Use a retpoline here to keep the RSB balanced.
	 */
	ANNOTATE_INTRA_FUNCTION_CALL
	call .Ldo_rop
	int3
.Ldo_rop:
	mov %rdi, (%rsp)
	UNWIND_HINT_FUNC
	RET
SYM_CODE_END(return_to_handler)
#endif
