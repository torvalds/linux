/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1998-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2003 Intel Co
 *	Suresh Siddha <suresh.b.siddha@intel.com>
 *	Fenghua Yu <fenghua.yu@intel.com>
 *	Arun Sharma <arun.sharma@intel.com>
 *
 * 12/07/98	S. Eranian	added pt_regs & switch_stack
 * 12/21/98	D. Mosberger	updated to match latest code
 *  6/17/99	D. Mosberger	added second unat member to "struct switch_stack"
 *
 */
#ifndef _ASM_IA64_PTRACE_H
#define _ASM_IA64_PTRACE_H

#ifndef ASM_OFFSETS_C
#include <asm/asm-offsets.h>
#endif
#include <uapi/asm/ptrace.h>

/*
 * Base-2 logarithm of number of pages to allocate per task structure
 * (including register backing store and memory stack):
 */
#if defined(CONFIG_IA64_PAGE_SIZE_4KB)
# define KERNEL_STACK_SIZE_ORDER		3
#elif defined(CONFIG_IA64_PAGE_SIZE_8KB)
# define KERNEL_STACK_SIZE_ORDER		2
#elif defined(CONFIG_IA64_PAGE_SIZE_16KB)
# define KERNEL_STACK_SIZE_ORDER		1
#else
# define KERNEL_STACK_SIZE_ORDER		0
#endif

#define IA64_RBS_OFFSET			((IA64_TASK_SIZE + IA64_THREAD_INFO_SIZE + 31) & ~31)
#define IA64_STK_OFFSET			((1 << KERNEL_STACK_SIZE_ORDER)*PAGE_SIZE)

#define KERNEL_STACK_SIZE		IA64_STK_OFFSET

#ifndef __ASSEMBLY__

#include <asm/current.h>
#include <asm/page.h>

/*
 * We use the ia64_psr(regs)->ri to determine which of the three
 * instructions in bundle (16 bytes) took the sample. Generate
 * the canonical representation by adding to instruction pointer.
 */
# define instruction_pointer(regs) ((regs)->cr_iip + ia64_psr(regs)->ri)

static inline unsigned long user_stack_pointer(struct pt_regs *regs)
{
	return regs->r12;
}

static inline int is_syscall_success(struct pt_regs *regs)
{
	return regs->r10 != -1;
}

static inline long regs_return_value(struct pt_regs *regs)
{
	if (is_syscall_success(regs))
		return regs->r8;
	else
		return -regs->r8;
}

/* Conserve space in histogram by encoding slot bits in address
 * bits 2 and 3 rather than bits 0 and 1.
 */
#define profile_pc(regs)						\
({									\
	unsigned long __ip = instruction_pointer(regs);			\
	(__ip & ~3UL) + ((__ip & 3UL) << 2);				\
})

  /* given a pointer to a task_struct, return the user's pt_regs */
# define task_pt_regs(t)		(((struct pt_regs *) ((char *) (t) + IA64_STK_OFFSET)) - 1)
# define ia64_psr(regs)			((struct ia64_psr *) &(regs)->cr_ipsr)
# define user_mode(regs)		(((struct ia64_psr *) &(regs)->cr_ipsr)->cpl != 0)
# define user_stack(task,regs)	((long) regs - (long) task == IA64_STK_OFFSET - sizeof(*regs))
# define fsys_mode(task,regs)					\
  ({								\
	  struct task_struct *_task = (task);			\
	  struct pt_regs *_regs = (regs);			\
	  !user_mode(_regs) && user_stack(_task, _regs);	\
  })

  /*
   * System call handlers that, upon successful completion, need to return a negative value
   * should call force_successful_syscall_return() right before returning.  On architectures
   * where the syscall convention provides for a separate error flag (e.g., alpha, ia64,
   * ppc{,64}, sparc{,64}, possibly others), this macro can be used to ensure that the error
   * flag will not get set.  On architectures which do not support a separate error flag,
   * the macro is a no-op and the spurious error condition needs to be filtered out by some
   * other means (e.g., in user-level, by passing an extra argument to the syscall handler,
   * or something along those lines).
   *
   * On ia64, we can clear the user's pt_regs->r8 to force a successful syscall.
   */
# define force_successful_syscall_return()	(task_pt_regs(current)->r8 = 0)

  struct task_struct;			/* forward decl */
  struct unw_frame_info;		/* forward decl */

  extern unsigned long ia64_get_user_rbs_end (struct task_struct *, struct pt_regs *,
					      unsigned long *);
  extern long ia64_peek (struct task_struct *, struct switch_stack *, unsigned long,
			 unsigned long, long *);
  extern long ia64_poke (struct task_struct *, struct switch_stack *, unsigned long,
			 unsigned long, long);
  extern void ia64_flush_fph (struct task_struct *);
  extern void ia64_sync_fph (struct task_struct *);
  extern void ia64_sync_krbs(void);
  extern long ia64_sync_user_rbs (struct task_struct *, struct switch_stack *,
				  unsigned long, unsigned long);

  /* get nat bits for scratch registers such that bit N==1 iff scratch register rN is a NaT */
  extern unsigned long ia64_get_scratch_nat_bits (struct pt_regs *pt, unsigned long scratch_unat);
  /* put nat bits for scratch registers such that scratch register rN is a NaT iff bit N==1 */
  extern unsigned long ia64_put_scratch_nat_bits (struct pt_regs *pt, unsigned long nat);

  extern void ia64_increment_ip (struct pt_regs *pt);
  extern void ia64_decrement_ip (struct pt_regs *pt);

  extern void ia64_ptrace_stop(void);
  #define arch_ptrace_stop(code, info) \
	ia64_ptrace_stop()
  #define arch_ptrace_stop_needed(code, info) \
	(!test_thread_flag(TIF_RESTORE_RSE))

  extern void ptrace_attach_sync_user_rbs (struct task_struct *);
  #define arch_ptrace_attach(child) \
	ptrace_attach_sync_user_rbs(child)

  #define arch_has_single_step()  (1)
  #define arch_has_block_step()   (1)

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_IA64_PTRACE_H */
