#ifndef _ASM_X86_FTRACE_H
#define _ASM_X86_FTRACE_H

#ifdef __ASSEMBLY__

	.macro MCOUNT_SAVE_FRAME
	/* taken from glibc */
	subq $0x38, %rsp
	movq %rax, (%rsp)
	movq %rcx, 8(%rsp)
	movq %rdx, 16(%rsp)
	movq %rsi, 24(%rsp)
	movq %rdi, 32(%rsp)
	movq %r8, 40(%rsp)
	movq %r9, 48(%rsp)
	.endm

	.macro MCOUNT_RESTORE_FRAME
	movq 48(%rsp), %r9
	movq 40(%rsp), %r8
	movq 32(%rsp), %rdi
	movq 24(%rsp), %rsi
	movq 16(%rsp), %rdx
	movq 8(%rsp), %rcx
	movq (%rsp), %rax
	addq $0x38, %rsp
	.endm

#endif

#ifdef CONFIG_FUNCTION_TRACER
#define MCOUNT_ADDR		((long)(mcount))
#define MCOUNT_INSN_SIZE	5 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void mcount(void);

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	/*
	 * call mcount is "e8 <4 byte offset>"
	 * The addr points to the 4 byte offset and the caller of this
	 * function wants the pointer to e8. Simply subtract one.
	 */
	return addr - 1;
}

#ifdef CONFIG_DYNAMIC_FTRACE

struct dyn_arch_ftrace {
	/* No extra data needed for x86 */
};

#endif /*  CONFIG_DYNAMIC_FTRACE */
#endif /* __ASSEMBLY__ */
#endif /* CONFIG_FUNCTION_TRACER */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

#ifndef __ASSEMBLY__

/*
 * Stack of return addresses for functions
 * of a thread.
 * Used in struct thread_info
 */
struct ftrace_ret_stack {
	unsigned long ret;
	unsigned long func;
	unsigned long long calltime;
};

/*
 * Primary handler of a function return.
 * It relays on ftrace_return_to_handler.
 * Defined in entry_32/64.S
 */
extern void return_to_handler(void);

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#endif /* _ASM_X86_FTRACE_H */
