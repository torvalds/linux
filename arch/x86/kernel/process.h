// SPDX-License-Identifier: GPL-2.0
//
// Code shared between 32 and 64 bit

void __switch_to_xtra(struct task_struct *prev_p, struct task_struct *next_p);

/*
 * This needs to be inline to optimize for the common case where no extra
 * work needs to be done.
 */
static inline void switch_to_extra(struct task_struct *prev,
				   struct task_struct *next)
{
	unsigned long next_tif = task_thread_info(next)->flags;
	unsigned long prev_tif = task_thread_info(prev)->flags;

	/*
	 * __switch_to_xtra() handles debug registers, i/o bitmaps,
	 * speculation mitigations etc.
	 */
	if (unlikely(next_tif & _TIF_WORK_CTXSW_NEXT ||
		     prev_tif & _TIF_WORK_CTXSW_PREV))
		__switch_to_xtra(prev, next);
}
