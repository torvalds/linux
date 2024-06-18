/* SPDX-License-Identifier: GPL-2.0 */
//
// Code shared between 32 and 64 bit

#include <asm/spec-ctrl.h>

void __switch_to_xtra(struct task_struct *prev_p, struct task_struct *next_p);

/*
 * This needs to be inline to optimize for the common case where no extra
 * work needs to be done.
 */
static inline void switch_to_extra(struct task_struct *prev,
				   struct task_struct *next)
{
	unsigned long next_tif = read_task_thread_flags(next);
	unsigned long prev_tif = read_task_thread_flags(prev);

	if (IS_ENABLED(CONFIG_SMP)) {
		/*
		 * Avoid __switch_to_xtra() invocation when conditional
		 * STIBP is disabled and the only different bit is
		 * TIF_SPEC_IB. For CONFIG_SMP=n TIF_SPEC_IB is not
		 * in the TIF_WORK_CTXSW masks.
		 */
		if (!static_branch_likely(&switch_to_cond_stibp)) {
			prev_tif &= ~_TIF_SPEC_IB;
			next_tif &= ~_TIF_SPEC_IB;
		}
	}

	/*
	 * __switch_to_xtra() handles debug registers, i/o bitmaps,
	 * speculation mitigations etc.
	 */
	if (unlikely(next_tif & _TIF_WORK_CTXSW_NEXT ||
		     prev_tif & _TIF_WORK_CTXSW_PREV))
		__switch_to_xtra(prev, next);
}
