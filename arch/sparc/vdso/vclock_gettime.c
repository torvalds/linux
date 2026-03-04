// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2006 Andi Kleen, SUSE Labs.
 *
 * Fast user context implementation of clock_gettime, gettimeofday, and time.
 *
 * The code should have no internal unresolved relocations.
 * Check with readelf after changing.
 * Also alternative() doesn't work.
 */
/*
 * Copyright (c) 2017 Oracle and/or its affiliates. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/io.h>
#include <asm/timex.h>
#include <asm/clocksource.h>
#include <asm/vdso/gettimeofday.h>
#include <asm/vvar.h>

/*
 * Compute the vvar page's address in the process address space, and return it
 * as a pointer to the vvar_data.
 */
notrace static __always_inline struct vvar_data *get_vvar_data(void)
{
	unsigned long ret;

	/*
	 * vdso data page is the first vDSO page so grab the PC
	 * and move up a page to get to the data page.
	 */
	__asm__("rd %%pc, %0" : "=r" (ret));
	ret &= ~(8192 - 1);
	ret -= 8192;

	return (struct vvar_data *) ret;
}

notrace static __always_inline u64 vgetsns(struct vvar_data *vvar)
{
	u64 v;
	u64 cycles = __arch_get_hw_counter(vvar);

	v = (cycles - vvar->clock.cycle_last) & vvar->clock.mask;
	return v * vvar->clock.mult;
}

notrace static __always_inline int do_realtime(struct vvar_data *vvar,
					       struct __kernel_old_timespec *ts)
{
	unsigned long seq;
	u64 ns;

	do {
		seq = vvar_read_begin(vvar);
		ts->tv_sec = vvar->wall_time_sec;
		ns = vvar->wall_time_snsec;
		ns += vgetsns(vvar);
		ns = vdso_shift_ns(ns, vvar->clock.shift);
	} while (unlikely(vvar_read_retry(vvar, seq)));

	ts->tv_sec += __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return 0;
}

notrace static __always_inline int do_monotonic(struct vvar_data *vvar,
						struct __kernel_old_timespec *ts)
{
	unsigned long seq;
	u64 ns;

	do {
		seq = vvar_read_begin(vvar);
		ts->tv_sec = vvar->monotonic_time_sec;
		ns = vvar->monotonic_time_snsec;
		ns += vgetsns(vvar);
		ns = vdso_shift_ns(ns, vvar->clock.shift);
	} while (unlikely(vvar_read_retry(vvar, seq)));

	ts->tv_sec += __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return 0;
}

notrace static int do_realtime_coarse(struct vvar_data *vvar,
				      struct __kernel_old_timespec *ts)
{
	unsigned long seq;

	do {
		seq = vvar_read_begin(vvar);
		ts->tv_sec = vvar->wall_time_coarse_sec;
		ts->tv_nsec = vvar->wall_time_coarse_nsec;
	} while (unlikely(vvar_read_retry(vvar, seq)));
	return 0;
}

notrace static int do_monotonic_coarse(struct vvar_data *vvar,
				       struct __kernel_old_timespec *ts)
{
	unsigned long seq;

	do {
		seq = vvar_read_begin(vvar);
		ts->tv_sec = vvar->monotonic_time_coarse_sec;
		ts->tv_nsec = vvar->monotonic_time_coarse_nsec;
	} while (unlikely(vvar_read_retry(vvar, seq)));

	return 0;
}

notrace int
__vdso_clock_gettime(clockid_t clock, struct __kernel_old_timespec *ts)
{
	struct vvar_data *vvd = get_vvar_data();

	switch (clock) {
	case CLOCK_REALTIME:
		if (unlikely(vvd->vclock_mode == VCLOCK_NONE))
			break;
		return do_realtime(vvd, ts);
	case CLOCK_MONOTONIC:
		if (unlikely(vvd->vclock_mode == VCLOCK_NONE))
			break;
		return do_monotonic(vvd, ts);
	case CLOCK_REALTIME_COARSE:
		return do_realtime_coarse(vvd, ts);
	case CLOCK_MONOTONIC_COARSE:
		return do_monotonic_coarse(vvd, ts);
	}
	/*
	 * Unknown clock ID ? Fall back to the syscall.
	 */
	return clock_gettime_fallback(clock, ts);
}
int
clock_gettime(clockid_t, struct __kernel_old_timespec *)
	__attribute__((weak, alias("__vdso_clock_gettime")));

notrace int
__vdso_gettimeofday(struct __kernel_old_timeval *tv, struct timezone *tz)
{
	struct vvar_data *vvd = get_vvar_data();

	if (likely(vvd->vclock_mode != VCLOCK_NONE)) {
		if (likely(tv != NULL)) {
			union tstv_t {
				struct __kernel_old_timespec ts;
				struct __kernel_old_timeval tv;
			} *tstv = (union tstv_t *) tv;
			do_realtime(vvd, &tstv->ts);
			/*
			 * Assign before dividing to ensure that the division is
			 * done in the type of tv_usec, not tv_nsec.
			 *
			 * There cannot be > 1 billion usec in a second:
			 * do_realtime() has already distributed such overflow
			 * into tv_sec.  So we can assign it to an int safely.
			 */
			tstv->tv.tv_usec = tstv->ts.tv_nsec;
			tstv->tv.tv_usec /= 1000;
		}
		if (unlikely(tz != NULL)) {
			/* Avoid memcpy. Some old compilers fail to inline it */
			tz->tz_minuteswest = vvd->tz_minuteswest;
			tz->tz_dsttime = vvd->tz_dsttime;
		}
		return 0;
	}
	return gettimeofday_fallback(tv, tz);
}
int
gettimeofday(struct __kernel_old_timeval *, struct timezone *)
	__attribute__((weak, alias("__vdso_gettimeofday")));
