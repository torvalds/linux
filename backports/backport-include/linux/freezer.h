#ifndef __BACKPORT_FREEZER_H_INCLUDED
#define __BACKPORT_FREEZER_H_INCLUDED
#include_next <linux/freezer.h>

#ifdef CONFIG_FREEZER
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0)
/*
 * Like schedule_hrtimeout_range(), but should not block the freezer.  Do not
 * call this with locks held.
 */
#define freezable_schedule_hrtimeout_range LINUX_BACKPORT(freezable_schedule_hrtimeout_range)
static inline int freezable_schedule_hrtimeout_range(ktime_t *expires,
		unsigned long delta, const enum hrtimer_mode mode)
{
	int __retval;
	freezer_do_not_count();
	__retval = schedule_hrtimeout_range(expires, delta, mode);
	freezer_count();
	return __retval;
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0) */

#else /* !CONFIG_FREEZER */

#ifndef freezable_schedule_hrtimeout_range
#define freezable_schedule_hrtimeout_range(expires, delta, mode)	\
	schedule_hrtimeout_range(expires, delta, mode)
#endif

#endif /* !CONFIG_FREEZER */

#endif /* __BACKPORT_FREEZER_H_INCLUDED */
