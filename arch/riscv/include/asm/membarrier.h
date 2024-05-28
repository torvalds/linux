/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_RISCV_MEMBARRIER_H
#define _ASM_RISCV_MEMBARRIER_H

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
	 *
	 * This barrier is also needed for the SYNC_CORE command when
	 * switching between processes; in particular, on a transition
	 * from a thread belonging to another mm to a thread belonging
	 * to the mm for which a membarrier SYNC_CORE is done on CPU0:
	 *
	 *   - [CPU0] sets all bits in the mm icache_stale_mask (in
	 *     prepare_sync_core_cmd());
	 *
	 *   - [CPU1] stores to rq->curr (by the scheduler);
	 *
	 *   - [CPU0] loads rq->curr within membarrier and observes
	 *     cpu_rq(1)->curr->mm != mm, so the IPI is skipped on
	 *     CPU1; this means membarrier relies on switch_mm() to
	 *     issue the sync-core;
	 *
	 *   - [CPU1] switch_mm() loads icache_stale_mask; if the bit
	 *     is zero, switch_mm() may incorrectly skip the sync-core.
	 *
	 * Matches a full barrier in the proximity of the membarrier
	 * system call entry.
	 */
	smp_mb();
}

#endif /* _ASM_RISCV_MEMBARRIER_H */
