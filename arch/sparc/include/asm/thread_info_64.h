/* thread_info.h: sparc64 low-level thread information
 *
 * Copyright (C) 2002  David S. Miller (davem@redhat.com)
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__

#define NSWINS		7

#define TI_FLAG_BYTE_FAULT_CODE		0
#define TI_FLAG_FAULT_CODE_SHIFT	56
#define TI_FLAG_BYTE_WSTATE		1
#define TI_FLAG_WSTATE_SHIFT		48
#define TI_FLAG_BYTE_CWP		2
#define TI_FLAG_CWP_SHIFT		40
#define TI_FLAG_BYTE_CURRENT_DS		3
#define TI_FLAG_CURRENT_DS_SHIFT	32
#define TI_FLAG_BYTE_FPDEPTH		4
#define TI_FLAG_FPDEPTH_SHIFT		24
#define TI_FLAG_BYTE_WSAVED		5
#define TI_FLAG_WSAVED_SHIFT		16

#include <asm/page.h>

#ifndef __ASSEMBLY__

#include <asm/ptrace.h>
#include <asm/types.h>

struct task_struct;
struct exec_domain;

struct thread_info {
	/* D$ line 1 */
	struct task_struct	*task;
	unsigned long		flags;
	__u8			fpsaved[7];
	__u8			status;
	unsigned long		ksp;

	/* D$ line 2 */
	unsigned long		fault_address;
	struct pt_regs		*kregs;
	struct exec_domain	*exec_domain;
	int			preempt_count;	/* 0 => preemptable, <0 => BUG */
	__u8			new_child;
	__u8			syscall_noerror;
	__u16			cpu;

	unsigned long		*utraps;

	struct reg_window 	reg_window[NSWINS];
	unsigned long 		rwbuf_stkptrs[NSWINS];

	unsigned long		gsr[7];
	unsigned long		xfsr[7];

	struct restart_block	restart_block;

	struct pt_regs		*kern_una_regs;
	unsigned int		kern_una_insn;

	unsigned long		fpregs[0] __attribute__ ((aligned(64)));
};

#endif /* !(__ASSEMBLY__) */

/* offsets into the thread_info struct for assembly code access */
#define TI_TASK		0x00000000
#define TI_FLAGS	0x00000008
#define TI_FAULT_CODE	(TI_FLAGS + TI_FLAG_BYTE_FAULT_CODE)
#define TI_WSTATE	(TI_FLAGS + TI_FLAG_BYTE_WSTATE)
#define TI_CWP		(TI_FLAGS + TI_FLAG_BYTE_CWP)
#define TI_CURRENT_DS	(TI_FLAGS + TI_FLAG_BYTE_CURRENT_DS)
#define TI_FPDEPTH	(TI_FLAGS + TI_FLAG_BYTE_FPDEPTH)
#define TI_WSAVED	(TI_FLAGS + TI_FLAG_BYTE_WSAVED)
#define TI_FPSAVED	0x00000010
#define TI_KSP		0x00000018
#define TI_FAULT_ADDR	0x00000020
#define TI_KREGS	0x00000028
#define TI_EXEC_DOMAIN	0x00000030
#define TI_PRE_COUNT	0x00000038
#define TI_NEW_CHILD	0x0000003c
#define TI_SYS_NOERROR	0x0000003d
#define TI_CPU		0x0000003e
#define TI_UTRAPS	0x00000040
#define TI_REG_WINDOW	0x00000048
#define TI_RWIN_SPTRS	0x000003c8
#define TI_GSR		0x00000400
#define TI_XFSR		0x00000438
#define TI_RESTART_BLOCK 0x00000470
#define TI_KUNA_REGS	0x000004a0
#define TI_KUNA_INSN	0x000004a8
#define TI_FPREGS	0x000004c0

/* We embed this in the uppermost byte of thread_info->flags */
#define FAULT_CODE_WRITE	0x01	/* Write access, implies D-TLB	   */
#define FAULT_CODE_DTLB		0x02	/* Miss happened in D-TLB	   */
#define FAULT_CODE_ITLB		0x04	/* Miss happened in I-TLB	   */
#define FAULT_CODE_WINFIXUP	0x08	/* Miss happened during spill/fill */
#define FAULT_CODE_BLKCOMMIT	0x10	/* Use blk-commit ASI in copy_page */

#if PAGE_SHIFT == 13
#define THREAD_SIZE (2*PAGE_SIZE)
#define THREAD_SHIFT (PAGE_SHIFT + 1)
#else /* PAGE_SHIFT == 13 */
#define THREAD_SIZE PAGE_SIZE
#define THREAD_SHIFT PAGE_SHIFT
#endif /* PAGE_SHIFT == 13 */

#define PREEMPT_ACTIVE		0x10000000

/*
 * macros/functions for gaining access to the thread information structure
 */
#ifndef __ASSEMBLY__

#define INIT_THREAD_INFO(tsk)				\
{							\
	.task		=	&tsk,			\
	.flags		= ((unsigned long)ASI_P) << TI_FLAG_CURRENT_DS_SHIFT,	\
	.exec_domain	=	&default_exec_domain,	\
	.preempt_count	=	INIT_PREEMPT_COUNT,	\
	.restart_block	= {				\
		.fn	=	do_no_restart_syscall,	\
	},						\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* how to get the thread information struct from C */
register struct thread_info *current_thread_info_reg asm("g6");
#define current_thread_info()	(current_thread_info_reg)

/* thread information allocation */
#if PAGE_SHIFT == 13
#define __THREAD_INFO_ORDER	1
#else /* PAGE_SHIFT == 13 */
#define __THREAD_INFO_ORDER	0
#endif /* PAGE_SHIFT == 13 */

#define __HAVE_ARCH_THREAD_INFO_ALLOCATOR

#ifdef CONFIG_DEBUG_STACK_USAGE
#define THREAD_FLAGS (GFP_KERNEL | __GFP_ZERO)
#else
#define THREAD_FLAGS (GFP_KERNEL)
#endif

#define alloc_thread_info_node(tsk, node)				\
({									\
	struct page *page = alloc_pages_node(node, THREAD_FLAGS,	\
					     __THREAD_INFO_ORDER);	\
	struct thread_info *ret;					\
									\
	ret = page ? page_address(page) : NULL;				\
	ret;								\
})

#define free_thread_info(ti) \
	free_pages((unsigned long)(ti),__THREAD_INFO_ORDER)

#define __thread_flag_byte_ptr(ti)	\
	((unsigned char *)(&((ti)->flags)))
#define __cur_thread_flag_byte_ptr	__thread_flag_byte_ptr(current_thread_info())

#define get_thread_fault_code()		(__cur_thread_flag_byte_ptr[TI_FLAG_BYTE_FAULT_CODE])
#define set_thread_fault_code(val)	(__cur_thread_flag_byte_ptr[TI_FLAG_BYTE_FAULT_CODE] = (val))
#define get_thread_wstate()		(__cur_thread_flag_byte_ptr[TI_FLAG_BYTE_WSTATE])
#define set_thread_wstate(val)		(__cur_thread_flag_byte_ptr[TI_FLAG_BYTE_WSTATE] = (val))
#define get_thread_cwp()		(__cur_thread_flag_byte_ptr[TI_FLAG_BYTE_CWP])
#define set_thread_cwp(val)		(__cur_thread_flag_byte_ptr[TI_FLAG_BYTE_CWP] = (val))
#define get_thread_current_ds()		(__cur_thread_flag_byte_ptr[TI_FLAG_BYTE_CURRENT_DS])
#define set_thread_current_ds(val)	(__cur_thread_flag_byte_ptr[TI_FLAG_BYTE_CURRENT_DS] = (val))
#define get_thread_fpdepth()		(__cur_thread_flag_byte_ptr[TI_FLAG_BYTE_FPDEPTH])
#define set_thread_fpdepth(val)		(__cur_thread_flag_byte_ptr[TI_FLAG_BYTE_FPDEPTH] = (val))
#define get_thread_wsaved()		(__cur_thread_flag_byte_ptr[TI_FLAG_BYTE_WSAVED])
#define set_thread_wsaved(val)		(__cur_thread_flag_byte_ptr[TI_FLAG_BYTE_WSAVED] = (val))

#endif /* !(__ASSEMBLY__) */

/*
 * Thread information flags, only 16 bits are available as we encode
 * other values into the upper 6 bytes.
 *
 * On trap return we need to test several values:
 *
 * user:	need_resched, notify_resume, sigpending, wsaved
 * kernel:	fpdepth
 *
 * So to check for work in the kernel case we simply load the fpdepth
 * byte out of the flags and test it.  For the user case we encode the
 * lower 3 bytes of flags as follows:
 *	----------------------------------------
 *	| wsaved | flags byte 1 | flags byte 2 |
 *	----------------------------------------
 * This optimizes the user test into:
 *	ldx		[%g6 + TI_FLAGS], REG1
 *	sethi		%hi(_TIF_USER_WORK_MASK), REG2
 *	or		REG2, %lo(_TIF_USER_WORK_MASK), REG2
 *	andcc		REG1, REG2, %g0
 *	be,pt		no_work_to_do
 *	 nop
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* callback before returning to user */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
/* flag bit 4 is available */
#define TIF_UNALIGNED		5	/* allowed to do unaligned accesses */
/* flag bit 6 is available */
#define TIF_32BIT		7	/* 32-bit binary */
/* flag bit 8 is available */
#define TIF_SECCOMP		9	/* secure computing */
#define TIF_SYSCALL_AUDIT	10	/* syscall auditing active */
#define TIF_SYSCALL_TRACEPOINT	11	/* syscall tracepoint instrumentation */
/* NOTE: Thread flags >= 12 should be ones we have no interest
 *       in using in assembly, else we can't use the mask as
 *       an immediate value in instructions such as andcc.
 */
/* flag bit 12 is available */
#define TIF_MEMDIE		13	/* is terminating due to OOM killer */
#define TIF_POLLING_NRFLAG	14
#define TIF_FREEZE		15	/* is freezing for suspend */

#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_UNALIGNED		(1<<TIF_UNALIGNED)
#define _TIF_32BIT		(1<<TIF_32BIT)
#define _TIF_SECCOMP		(1<<TIF_SECCOMP)
#define _TIF_SYSCALL_AUDIT	(1<<TIF_SYSCALL_AUDIT)
#define _TIF_SYSCALL_TRACEPOINT	(1<<TIF_SYSCALL_TRACEPOINT)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)
#define _TIF_FREEZE		(1<<TIF_FREEZE)

#define _TIF_USER_WORK_MASK	((0xff << TI_FLAG_WSAVED_SHIFT) | \
				 _TIF_DO_NOTIFY_RESUME_MASK | \
				 _TIF_NEED_RESCHED)
#define _TIF_DO_NOTIFY_RESUME_MASK	(_TIF_NOTIFY_RESUME | _TIF_SIGPENDING)

/*
 * Thread-synchronous status.
 *
 * This is different from the flags in that nobody else
 * ever touches our thread-synchronous status, so we don't
 * have to worry about atomic accesses.
 *
 * Note that there are only 8 bits available.
 */
#define TS_RESTORE_SIGMASK	0x0001	/* restore signal mask in do_signal() */

#ifndef __ASSEMBLY__
#define HAVE_SET_RESTORE_SIGMASK	1
static inline void set_restore_sigmask(void)
{
	struct thread_info *ti = current_thread_info();
	ti->status |= TS_RESTORE_SIGMASK;
	set_bit(TIF_SIGPENDING, &ti->flags);
}
#endif	/* !__ASSEMBLY__ */

#endif /* __KERNEL__ */

#endif /* _ASM_THREAD_INFO_H */
