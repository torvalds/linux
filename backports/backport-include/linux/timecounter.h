#ifndef __BACKPORT_LINUX_TIMECOUNTER_H
#define __BACKPORT_LINUX_TIMECOUNTER_H

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,20,0)
#include_next <linux/timecounter.h>
#else
#include <linux/clocksource.h>

/**
 * timecounter_adjtime - Shifts the time of the clock.
 * @delta:	Desired change in nanoseconds.
 */
#define timecounter_adjtime LINUX_BACKPORT(timecounter_adjtime)
static inline void timecounter_adjtime(struct timecounter *tc, s64 delta)
{
	tc->nsec += delta;
}
#endif

#ifndef CYCLECOUNTER_MASK
/* simplify initialization of mask field */
#define CYCLECOUNTER_MASK(bits) (cycle_t)((bits) < 64 ? ((1ULL<<(bits))-1) : -1)
#endif

#endif /* __BACKPORT_LINUX_TIMECOUNTER_H */
