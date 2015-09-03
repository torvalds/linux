/* thread_info.h: MIPS low-level thread information
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__


#ifndef __ASSEMBLY__

#include <asm/processor.h>

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 * - if the contents of this structure are changed, the assembly constants
 *   must also be changed
 */
struct thread_info {
	struct task_struct	*task;		/* main task structure */
	unsigned long		flags;		/* low level flags */
	unsigned long		tp_value;	/* thread pointer */
	__u32			cpu;		/* current CPU */
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */
	int			r2_emul_return; /* 1 => Returning from R2 emulator */
	mm_segment_t		addr_limit;	/*
						 * thread address space limit:
						 * 0x7fffffff for user-thead
						 * 0xffffffff for kernel-thread
						 */
	struct pt_regs		*regs;
	long			syscall;	/* syscall number */
};

/*
 * macros/functions for gaining access to the thread information structure
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.task		= &tsk,			\
	.flags		= _TIF_FIXADE,		\
	.cpu		= 0,			\
	.preempt_count	= INIT_PREEMPT_COUNT,	\
	.addr_limit	= KERNEL_DS,		\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* How to get the thread information struct from C.  */
register struct thread_info *__current_thread_info __asm__("$28");

static inline struct thread_info *current_thread_info(void)
{
	return __current_thread_info;
}

#endif /* !__ASSEMBLY__ */

/* thread information allocation */
#if defined(CONFIG_PAGE_SIZE_4KB) && defined(CONFIG_32BIT)
#define THREAD_SIZE_ORDER (1)
#endif
#if defined(CONFIG_PAGE_SIZE_4KB) && defined(CONFIG_64BIT)
#define THREAD_SIZE_ORDER (2)
#endif
#ifdef CONFIG_PAGE_SIZE_8KB
#define THREAD_SIZE_ORDER (1)
#endif
#ifdef CONFIG_PAGE_SIZE_16KB
#define THREAD_SIZE_ORDER (0)
#endif
#ifdef CONFIG_PAGE_SIZE_32KB
#define THREAD_SIZE_ORDER (0)
#endif
#ifdef CONFIG_PAGE_SIZE_64KB
#define THREAD_SIZE_ORDER (0)
#endif

#define THREAD_SIZE (PAGE_SIZE << THREAD_SIZE_ORDER)
#define THREAD_MASK (THREAD_SIZE - 1UL)

#define STACK_WARN	(THREAD_SIZE / 8)

/*
 * thread information flags
 * - these are process state flags that various assembly files may need to
 *   access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_SYSCALL_AUDIT	3	/* syscall auditing active */
#define TIF_SECCOMP		4	/* secure computing */
#define TIF_NOTIFY_RESUME	5	/* callback before returning to user */
#define TIF_RESTORE_SIGMASK	9	/* restore signal mask in do_signal() */
#define TIF_USEDFPU		16	/* FPU was used by this task this quantum (SMP) */
#define TIF_MEMDIE		18	/* is terminating due to OOM killer */
#define TIF_NOHZ		19	/* in adaptive nohz mode */
#define TIF_FIXADE		20	/* Fix address errors in software */
#define TIF_LOGADE		21	/* Log address errors to syslog */
#define TIF_32BIT_REGS		22	/* 32-bit general purpose registers */
#define TIF_32BIT_ADDR		23	/* 32-bit address space (o32/n32) */
#define TIF_FPUBOUND		24	/* thread bound to FPU-full CPU set */
#define TIF_LOAD_WATCH		25	/* If set, load watch registers */
#define TIF_SYSCALL_TRACEPOINT	26	/* syscall tracepoint instrumentation */
#define TIF_32BIT_FPREGS	27	/* 32-bit floating point registers */
#define TIF_HYBRID_FPREGS	28	/* 64b FP registers, odd singles in bits 63:32 of even doubles */
#define TIF_USEDMSA		29	/* MSA has been used this quantum */
#define TIF_MSA_CTX_LIVE	30	/* MSA context must be preserved */
#define TIF_SYSCALL_TRACE	31	/* syscall trace active */

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_SYSCALL_AUDIT	(1<<TIF_SYSCALL_AUDIT)
#define _TIF_SECCOMP		(1<<TIF_SECCOMP)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_USEDFPU		(1<<TIF_USEDFPU)
#define _TIF_NOHZ		(1<<TIF_NOHZ)
#define _TIF_FIXADE		(1<<TIF_FIXADE)
#define _TIF_LOGADE		(1<<TIF_LOGADE)
#define _TIF_32BIT_REGS		(1<<TIF_32BIT_REGS)
#define _TIF_32BIT_ADDR		(1<<TIF_32BIT_ADDR)
#define _TIF_FPUBOUND		(1<<TIF_FPUBOUND)
#define _TIF_LOAD_WATCH		(1<<TIF_LOAD_WATCH)
#define _TIF_32BIT_FPREGS	(1<<TIF_32BIT_FPREGS)
#define _TIF_HYBRID_FPREGS	(1<<TIF_HYBRID_FPREGS)
#define _TIF_USEDMSA		(1<<TIF_USEDMSA)
#define _TIF_MSA_CTX_LIVE	(1<<TIF_MSA_CTX_LIVE)
#define _TIF_SYSCALL_TRACEPOINT	(1<<TIF_SYSCALL_TRACEPOINT)

#define _TIF_WORK_SYSCALL_ENTRY	(_TIF_NOHZ | _TIF_SYSCALL_TRACE |	\
				 _TIF_SYSCALL_AUDIT | \
				 _TIF_SYSCALL_TRACEPOINT | _TIF_SECCOMP)

/* work to do in syscall_trace_leave() */
#define _TIF_WORK_SYSCALL_EXIT	(_TIF_NOHZ | _TIF_SYSCALL_TRACE |	\
				 _TIF_SYSCALL_AUDIT | _TIF_SYSCALL_TRACEPOINT)

/* work to do on interrupt/exception return */
#define _TIF_WORK_MASK		\
	(_TIF_SIGPENDING | _TIF_NEED_RESCHED | _TIF_NOTIFY_RESUME)
/* work to do on any return to u-space */
#define _TIF_ALLWORK_MASK	(_TIF_NOHZ | _TIF_WORK_MASK |		\
				 _TIF_WORK_SYSCALL_EXIT |		\
				 _TIF_SYSCALL_TRACEPOINT)

/*
 * We stash processor id into a COP0 register to retrieve it fast
 * at kernel exception entry.
 */
#if   defined(CONFIG_MIPS_PGD_C0_CONTEXT)
#define SMP_CPUID_REG		20, 0	/* XCONTEXT */
#define ASM_SMP_CPUID_REG	$20
#define SMP_CPUID_PTRSHIFT	48
#else
#define SMP_CPUID_REG		4, 0	/* CONTEXT */
#define ASM_SMP_CPUID_REG	$4
#define SMP_CPUID_PTRSHIFT	23
#endif

#ifdef CONFIG_64BIT
#define SMP_CPUID_REGSHIFT	(SMP_CPUID_PTRSHIFT + 3)
#else
#define SMP_CPUID_REGSHIFT	(SMP_CPUID_PTRSHIFT + 2)
#endif

#define ASM_CPUID_MFC0		MFC0
#define UASM_i_CPUID_MFC0	UASM_i_MFC0

#endif /* __KERNEL__ */
#endif /* _ASM_THREAD_INFO_H */
