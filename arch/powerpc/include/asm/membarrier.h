#ifndef _ASM_POWERPC_MEMBARRIER_H
#define _ASM_POWERPC_MEMBARRIER_H

static inline void membarrier_arch_switch_mm(struct mm_struct *prev,
					     struct mm_struct *next,
					     struct task_struct *tsk)
{
	/*
	 * Only need the full barrier when switching between processes.
	 * Barrier when switching from kernel to userspace is not
	 * required here, given that it is implied by mmdrop(). Barrier
	 * when switching from userspace to kernel is not needed after
	 * store to rq->curr.
	 */
	if (IS_ENABLED(CONFIG_SMP) &&
	    likely(!(atomic_read(&next->membarrier_state) &
		     (MEMBARRIER_STATE_PRIVATE_EXPEDITED |
		      MEMBARRIER_STATE_GLOBAL_EXPEDITED)) || !prev))
		return;

	/*
	 * The membarrier system call requires a full memory barrier
	 * after storing to rq->curr, before going back to user-space.
	 */
	smp_mb();
}

#endif /* _ASM_POWERPC_MEMBARRIER_H */
