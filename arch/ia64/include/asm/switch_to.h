/*
 * Low-level task switching. This is based on information published in
 * the Processor Abstraction Layer and the System Abstraction Layer
 * manual.
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Asit Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 */
#ifndef _ASM_IA64_SWITCH_TO_H
#define _ASM_IA64_SWITCH_TO_H

#include <linux/percpu.h>

struct task_struct;

/*
 * Context switch from one thread to another.  If the two threads have
 * different address spaces, schedule() has already taken care of
 * switching to the new address space by calling switch_mm().
 *
 * Disabling access to the fph partition and the debug-register
 * context switch MUST be done before calling ia64_switch_to() since a
 * newly created thread returns directly to
 * ia64_ret_from_syscall_clear_r8.
 */
extern struct task_struct *ia64_switch_to (void *next_task);

extern void ia64_save_extra (struct task_struct *task);
extern void ia64_load_extra (struct task_struct *task);

#ifdef CONFIG_PERFMON
  DECLARE_PER_CPU(unsigned long, pfm_syst_info);
# define PERFMON_IS_SYSWIDE() (__this_cpu_read(pfm_syst_info) & 0x1)
#else
# define PERFMON_IS_SYSWIDE() (0)
#endif

#define IA64_HAS_EXTRA_STATE(t)							\
	((t)->thread.flags & (IA64_THREAD_DBG_VALID|IA64_THREAD_PM_VALID)	\
	 || PERFMON_IS_SYSWIDE())

#define __switch_to(prev,next,last) do {							 \
	if (IA64_HAS_EXTRA_STATE(prev))								 \
		ia64_save_extra(prev);								 \
	if (IA64_HAS_EXTRA_STATE(next))								 \
		ia64_load_extra(next);								 \
	ia64_psr(task_pt_regs(next))->dfh = !ia64_is_local_fpu_owner(next);			 \
	(last) = ia64_switch_to((next));							 \
} while (0)

#ifdef CONFIG_SMP
/*
 * In the SMP case, we save the fph state when context-switching away from a thread that
 * modified fph.  This way, when the thread gets scheduled on another CPU, the CPU can
 * pick up the state from task->thread.fph, avoiding the complication of having to fetch
 * the latest fph state from another CPU.  In other words: eager save, lazy restore.
 */
# define switch_to(prev,next,last) do {						\
	if (ia64_psr(task_pt_regs(prev))->mfh && ia64_is_local_fpu_owner(prev)) {				\
		ia64_psr(task_pt_regs(prev))->mfh = 0;			\
		(prev)->thread.flags |= IA64_THREAD_FPH_VALID;			\
		__ia64_save_fpu((prev)->thread.fph);				\
	}									\
	__switch_to(prev, next, last);						\
	/* "next" in old context is "current" in new context */			\
	if (unlikely((current->thread.flags & IA64_THREAD_MIGRATION) &&	       \
		     (task_cpu(current) !=				       \
		      		      task_thread_info(current)->last_cpu))) { \
		platform_migrate(current);				       \
		task_thread_info(current)->last_cpu = task_cpu(current);       \
	}								       \
} while (0)
#else
# define switch_to(prev,next,last)	__switch_to(prev, next, last)
#endif

#endif /* _ASM_IA64_SWITCH_TO_H */
