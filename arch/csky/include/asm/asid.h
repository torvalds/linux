/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ASM_ASID_H
#define __ASM_ASM_ASID_H

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>

struct asid_info
{
	atomic64_t	generation;
	unsigned long	*map;
	atomic64_t __percpu	*active;
	u64 __percpu		*reserved;
	u32			bits;
	/* Lock protecting the structure */
	raw_spinlock_t		lock;
	/* Which CPU requires context flush on next call */
	cpumask_t		flush_pending;
	/* Number of ASID allocated by context (shift value) */
	unsigned int		ctxt_shift;
	/* Callback to locally flush the context. */
	void			(*flush_cpu_ctxt_cb)(void);
};

#define NUM_ASIDS(info)			(1UL << ((info)->bits))
#define NUM_CTXT_ASIDS(info)		(NUM_ASIDS(info) >> (info)->ctxt_shift)

#define active_asid(info, cpu)	*per_cpu_ptr((info)->active, cpu)

void asid_new_context(struct asid_info *info, atomic64_t *pasid,
		      unsigned int cpu, struct mm_struct *mm);

/*
 * Check the ASID is still valid for the context. If not generate a new ASID.
 *
 * @pasid: Pointer to the current ASID batch
 * @cpu: current CPU ID. Must have been acquired throught get_cpu()
 */
static inline void asid_check_context(struct asid_info *info,
				      atomic64_t *pasid, unsigned int cpu,
				      struct mm_struct *mm)
{
	u64 asid, old_active_asid;

	asid = atomic64_read(pasid);

	/*
	 * The memory ordering here is subtle.
	 * If our active_asid is non-zero and the ASID matches the current
	 * generation, then we update the active_asid entry with a relaxed
	 * cmpxchg. Racing with a concurrent rollover means that either:
	 *
	 * - We get a zero back from the cmpxchg and end up waiting on the
	 *   lock. Taking the lock synchronises with the rollover and so
	 *   we are forced to see the updated generation.
	 *
	 * - We get a valid ASID back from the cmpxchg, which means the
	 *   relaxed xchg in flush_context will treat us as reserved
	 *   because atomic RmWs are totally ordered for a given location.
	 */
	old_active_asid = atomic64_read(&active_asid(info, cpu));
	if (old_active_asid &&
	    !((asid ^ atomic64_read(&info->generation)) >> info->bits) &&
	    atomic64_cmpxchg_relaxed(&active_asid(info, cpu),
				     old_active_asid, asid))
		return;

	asid_new_context(info, pasid, cpu, mm);
}

int asid_allocator_init(struct asid_info *info,
			u32 bits, unsigned int asid_per_ctxt,
			void (*flush_cpu_ctxt_cb)(void));

#endif
