/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARM_SWITCH_TO_H
#define __ASM_ARM_SWITCH_TO_H

#include <linux/thread_info.h>
#include <asm/smp_plat.h>

/*
 * For v7 SMP cores running a preemptible kernel we may be pre-empted
 * during a TLB maintenance operation, so execute an inner-shareable dsb
 * to ensure that the maintenance completes in case we migrate to another
 * CPU.
 */
#if defined(CONFIG_PREEMPTION) && defined(CONFIG_SMP) && defined(CONFIG_CPU_V7)
#define __complete_pending_tlbi()	dsb(ish)
#else
#define __complete_pending_tlbi()
#endif

/*
 * switch_to(prev, next) should switch from task `prev' to `next'
 * `prev' will never be the same as `next'.  schedule() itself
 * contains the memory barrier to tell GCC not to cache `current'.
 */
extern struct task_struct *__switch_to(struct task_struct *, struct thread_info *, struct thread_info *);

static inline void set_ti_cpu(struct task_struct *p)
{
#ifdef CONFIG_THREAD_INFO_IN_TASK
	/*
	 * The core code no longer maintains the thread_info::cpu field once
	 * CONFIG_THREAD_INFO_IN_TASK is in effect, but we rely on it for
	 * raw_smp_processor_id(), which cannot access struct task_struct*
	 * directly for reasons of circular #inclusion hell.
	 */
	task_thread_info(p)->cpu = task_cpu(p);
#endif
}

#define switch_to(prev,next,last)					\
do {									\
	__complete_pending_tlbi();					\
	set_ti_cpu(next);						\
	if (IS_ENABLED(CONFIG_CURRENT_POINTER_IN_TPIDRURO) || is_smp())	\
		__this_cpu_write(__entry_task, next);			\
	last = __switch_to(prev,task_thread_info(prev), task_thread_info(next));	\
} while (0)

#endif /* __ASM_ARM_SWITCH_TO_H */
