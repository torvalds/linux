#ifndef _ASM_X86_SWITCH_TO_H
#define _ASM_X86_SWITCH_TO_H

#include <asm/nospec-branch.h>

struct task_struct; /* one of the stranger aspects of C forward declarations */
__visible struct task_struct *__switch_to(struct task_struct *prev,
					   struct task_struct *next);
struct tss_struct;
void __switch_to_xtra(struct task_struct *prev_p, struct task_struct *next_p,
		      struct tss_struct *tss);

#ifdef CONFIG_X86_32

#ifdef CONFIG_CC_STACKPROTECTOR
#define __switch_canary							\
	"movl %P[task_canary](%[next]), %%ebx\n\t"			\
	"movl %%ebx, "__percpu_arg([stack_canary])"\n\t"
#define __switch_canary_oparam						\
	, [stack_canary] "=m" (stack_canary.canary)
#define __switch_canary_iparam						\
	, [task_canary] "i" (offsetof(struct task_struct, stack_canary))
#else	/* CC_STACKPROTECTOR */
#define __switch_canary
#define __switch_canary_oparam
#define __switch_canary_iparam
#endif	/* CC_STACKPROTECTOR */

#ifdef CONFIG_RETPOLINE
	/*
	 * When switching from a shallower to a deeper call stack
	 * the RSB may either underflow or use entries populated
	 * with userspace addresses. On CPUs where those concerns
	 * exist, overwrite the RSB with entries which capture
	 * speculative execution to prevent attack.
	 */
#define __retpoline_fill_return_buffer					\
	ALTERNATIVE("jmp 910f",						\
		__stringify(__FILL_RETURN_BUFFER(%%ebx, RSB_CLEAR_LOOPS, %%esp)),\
		X86_FEATURE_RSB_CTXSW)					\
	"910:\n\t"
#else
#define __retpoline_fill_return_buffer
#endif

/*
 * Saving eflags is important. It switches not only IOPL between tasks,
 * it also protects other tasks from NT leaking through sysenter etc.
 */
#define switch_to(prev, next, last)					\
do {									\
	/*								\
	 * Context-switching clobbers all registers, so we clobber	\
	 * them explicitly, via unused output variables.		\
	 * (EAX and EBP is not listed because EBP is saved/restored	\
	 * explicitly for wchan access and EAX is the return value of	\
	 * __switch_to())						\
	 */								\
	unsigned long ebx, ecx, edx, esi, edi;				\
									\
	asm volatile("pushfl\n\t"		/* save    flags */	\
		     "pushl %%ebp\n\t"		/* save    EBP   */	\
		     "movl %%esp,%[prev_sp]\n\t"	/* save    ESP   */ \
		     "movl %[next_sp],%%esp\n\t"	/* restore ESP   */ \
		     "movl $1f,%[prev_ip]\n\t"	/* save    EIP   */	\
		     "pushl %[next_ip]\n\t"	/* restore EIP   */	\
		     __switch_canary					\
		     __retpoline_fill_return_buffer			\
		     "jmp __switch_to\n"	/* regparm call  */	\
		     "1:\t"						\
		     "popl %%ebp\n\t"		/* restore EBP   */	\
		     "popfl\n"			/* restore flags */	\
									\
		     /* output parameters */				\
		     : [prev_sp] "=m" (prev->thread.sp),		\
		       [prev_ip] "=m" (prev->thread.ip),		\
		       "=a" (last),					\
									\
		       /* clobbered output registers: */		\
		       "=b" (ebx), "=c" (ecx), "=d" (edx),		\
		       "=S" (esi), "=D" (edi)				\
		       							\
		       __switch_canary_oparam				\
									\
		       /* input parameters: */				\
		     : [next_sp]  "m" (next->thread.sp),		\
		       [next_ip]  "m" (next->thread.ip),		\
		       							\
		       /* regparm parameters for __switch_to(): */	\
		       [prev]     "a" (prev),				\
		       [next]     "d" (next)				\
									\
		       __switch_canary_iparam				\
									\
		     : /* reloaded segment registers */			\
			"memory");					\
} while (0)

#else /* CONFIG_X86_32 */

/* frame pointer must be last for get_wchan */
#define SAVE_CONTEXT    "pushq %%rbp ; movq %%rsi,%%rbp\n\t"
#define RESTORE_CONTEXT "movq %%rbp,%%rsi ; popq %%rbp\t"

#define __EXTRA_CLOBBER  \
	, "rcx", "rbx", "rdx", "r8", "r9", "r10", "r11", \
	  "r12", "r13", "r14", "r15", "flags"

#ifdef CONFIG_CC_STACKPROTECTOR
#define __switch_canary							  \
	"movq %P[task_canary](%%rsi),%%r8\n\t"				  \
	"movq %%r8,"__percpu_arg([gs_canary])"\n\t"
#define __switch_canary_oparam						  \
	, [gs_canary] "=m" (irq_stack_union.stack_canary)
#define __switch_canary_iparam						  \
	, [task_canary] "i" (offsetof(struct task_struct, stack_canary))
#else	/* CC_STACKPROTECTOR */
#define __switch_canary
#define __switch_canary_oparam
#define __switch_canary_iparam
#endif	/* CC_STACKPROTECTOR */

#ifdef CONFIG_RETPOLINE
	/*
	 * When switching from a shallower to a deeper call stack
	 * the RSB may either underflow or use entries populated
	 * with userspace addresses. On CPUs where those concerns
	 * exist, overwrite the RSB with entries which capture
	 * speculative execution to prevent attack.
	 */
#define __retpoline_fill_return_buffer					\
	ALTERNATIVE("jmp 910f",						\
		__stringify(__FILL_RETURN_BUFFER(%%r12, RSB_CLEAR_LOOPS, %%rsp)),\
		X86_FEATURE_RSB_CTXSW)					\
	"910:\n\t"
#else
#define __retpoline_fill_return_buffer
#endif

/*
 * There is no need to save or restore flags, because flags are always
 * clean in kernel mode, with the possible exception of IOPL.  Kernel IOPL
 * has no effect.
 */
#define switch_to(prev, next, last) \
	asm volatile(SAVE_CONTEXT					  \
	     "movq %%rsp,%P[threadrsp](%[prev])\n\t" /* save RSP */	  \
	     "movq %P[threadrsp](%[next]),%%rsp\n\t" /* restore RSP */	  \
	     "call __switch_to\n\t"					  \
	     "movq "__percpu_arg([current_task])",%%rsi\n\t"		  \
	     __switch_canary						  \
	     __retpoline_fill_return_buffer				  \
	     "movq %P[thread_info](%%rsi),%%r8\n\t"			  \
	     "movq %%rax,%%rdi\n\t" 					  \
	     "testl  %[_tif_fork],%P[ti_flags](%%r8)\n\t"		  \
	     "jnz   ret_from_fork\n\t"					  \
	     RESTORE_CONTEXT						  \
	     : "=a" (last)					  	  \
	       __switch_canary_oparam					  \
	     : [next] "S" (next), [prev] "D" (prev),			  \
	       [threadrsp] "i" (offsetof(struct task_struct, thread.sp)), \
	       [ti_flags] "i" (offsetof(struct thread_info, flags)),	  \
	       [_tif_fork] "i" (_TIF_FORK),			  	  \
	       [thread_info] "i" (offsetof(struct task_struct, stack)),   \
	       [current_task] "m" (current_task)			  \
	       __switch_canary_iparam					  \
	     : "memory", "cc" __EXTRA_CLOBBER)

#endif /* CONFIG_X86_32 */

#endif /* _ASM_X86_SWITCH_TO_H */
