/*
 * Copyright 2006 Andi Kleen, SUSE Labs.
 * Subject to the GNU Public License, v.2
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
#include <linux/time.h>
#include <linux/string.h>
#include <asm/io.h>
#include <asm/unistd.h>
#include <asm/timex.h>
#include <asm/clocksource.h>
#include <asm/vvar.h>

#ifdef	CONFIG_SPARC64
#define SYSCALL_STRING							\
	"ta	0x6d;"							\
	"bcs,a	1f;"							\
	" sub	%%g0, %%o0, %%o0;"					\
	"1:"
#else
#define SYSCALL_STRING							\
	"ta	0x10;"							\
	"bcs,a	1f;"							\
	" sub	%%g0, %%o0, %%o0;"					\
	"1:"
#endif

#define SYSCALL_CLOBBERS						\
	"f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7",			\
	"f8", "f9", "f10", "f11", "f12", "f13", "f14", "f15",		\
	"f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",		\
	"f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",		\
	"f32", "f34", "f36", "f38", "f40", "f42", "f44", "f46",		\
	"f48", "f50", "f52", "f54", "f56", "f58", "f60", "f62",		\
	"cc", "memory"

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

notrace static long vdso_fallback_gettime(long clock, struct timespec *ts)
{
	register long num __asm__("g1") = __NR_clock_gettime;
	register long o0 __asm__("o0") = clock;
	register long o1 __asm__("o1") = (long) ts;

	__asm__ __volatile__(SYSCALL_STRING : "=r" (o0) : "r" (num),
			     "0" (o0), "r" (o1) : SYSCALL_CLOBBERS);
	return o0;
}

notrace static long vdso_fallback_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	register long num __asm__("g1") = __NR_gettimeofday;
	register long o0 __asm__("o0") = (long) tv;
	register long o1 __asm__("o1") = (long) tz;

	__asm__ __volatile__(SYSCALL_STRING : "=r" (o0) : "r" (num),
			     "0" (o0), "r" (o1) : SYSCALL_CLOBBERS);
	return o0;
}

#ifdef	CONFIG_SPARC64
notrace static __always_inline u64 vread_tick(void)
{
	u64	ret;

	__asm__ __volatile__("rd %%tick, %0" : "=r" (ret));
	return ret;
}

notrace static __always_inline u64 vread_tick_stick(void)
{
	u64	ret;

	__asm__ __volatile__("rd %%asr24, %0" : "=r" (ret));
	return ret;
}
#else
notrace static __always_inline u64 vread_tick(void)
{
	register unsigned long long ret asm("o4");

	__asm__ __volatile__("rd %%tick, %L0\n\t"
			     "srlx %L0, 32, %H0"
			     : "=r" (ret));
	return ret;
}

notrace static __always_inline u64 vread_tick_stick(void)
{
	register unsigned long long ret asm("o4");

	__asm__ __volatile__("rd %%asr24, %L0\n\t"
			     "srlx %L0, 32, %H0"
			     : "=r" (ret));
	return ret;
}
#endif

notrace static __always_inline u64 vgetsns(struct vvar_data *vvar)
{
	u64 v;
	u64 cycles;

	cycles = vread_tick();
	v = (cycles - vvar->clock.cycle_last) & vvar->clock.mask;
	return v * vvar->clock.mult;
}

notrace static __always_inline u64 vgetsns_stick(struct vvar_data *vvar)
{
	u64 v;
	u64 cycles;

	cycles = vread_tick_stick();
	v = (cycles - vvar->clock.cycle_last) & vvar->clock.mask;
	return v * vvar->clock.mult;
}

notrace static __always_inline int do_realtime(struct vvar_data *vvar,
					       struct timespec *ts)
{
	unsigned long seq;
	u64 ns;

	do {
		seq = vvar_read_begin(vvar);
		ts->tv_sec = vvar->wall_time_sec;
		ns = vvar->wall_time_snsec;
		ns += vgetsns(vvar);
		ns >>= vvar->clock.shift;
	} while (unlikely(vvar_read_retry(vvar, seq)));

	ts->tv_sec += __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return 0;
}

notrace static __always_inline int do_realtime_stick(struct vvar_data *vvar,
						     struct timespec *ts)
{
	unsigned long seq;
	u64 ns;

	do {
		seq = vvar_read_begin(vvar);
		ts->tv_sec = vvar->wall_time_sec;
		ns = vvar->wall_time_snsec;
		ns += vgetsns_stick(vvar);
		ns >>= vvar->clock.shift;
	} while (unlikely(vvar_read_retry(vvar, seq)));

	ts->tv_sec += __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return 0;
}

notrace static __always_inline int do_monotonic(struct vvar_data *vvar,
						struct timespec *ts)
{
	unsigned long seq;
	u64 ns;

	do {
		seq = vvar_read_begin(vvar);
		ts->tv_sec = vvar->monotonic_time_sec;
		ns = vvar->monotonic_time_snsec;
		ns += vgetsns(vvar);
		ns >>= vvar->clock.shift;
	} while (unlikely(vvar_read_retry(vvar, seq)));

	ts->tv_sec += __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return 0;
}

notrace static __always_inline int do_monotonic_stick(struct vvar_data *vvar,
						      struct timespec *ts)
{
	unsigned long seq;
	u64 ns;

	do {
		seq = vvar_read_begin(vvar);
		ts->tv_sec = vvar->monotonic_time_sec;
		ns = vvar->monotonic_time_snsec;
		ns += vgetsns_stick(vvar);
		ns >>= vvar->clock.shift;
	} while (unlikely(vvar_read_retry(vvar, seq)));

	ts->tv_sec += __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return 0;
}

notrace static int do_realtime_coarse(struct vvar_data *vvar,
				      struct timespec *ts)
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
				       struct timespec *ts)
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
__vdso_clock_gettime(clockid_t clock, struct timespec *ts)
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
	return vdso_fallback_gettime(clock, ts);
}
int
clock_gettime(clockid_t, struct timespec *)
	__attribute__((weak, alias("__vdso_clock_gettime")));

notrace int
__vdso_clock_gettime_stick(clockid_t clock, struct timespec *ts)
{
	struct vvar_data *vvd = get_vvar_data();

	switch (clock) {
	case CLOCK_REALTIME:
		if (unlikely(vvd->vclock_mode == VCLOCK_NONE))
			break;
		return do_realtime_stick(vvd, ts);
	case CLOCK_MONOTONIC:
		if (unlikely(vvd->vclock_mode == VCLOCK_NONE))
			break;
		return do_monotonic_stick(vvd, ts);
	case CLOCK_REALTIME_COARSE:
		return do_realtime_coarse(vvd, ts);
	case CLOCK_MONOTONIC_COARSE:
		return do_monotonic_coarse(vvd, ts);
	}
	/*
	 * Unknown clock ID ? Fall back to the syscall.
	 */
	return vdso_fallback_gettime(clock, ts);
}

notrace int
__vdso_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	struct vvar_data *vvd = get_vvar_data();

	if (likely(vvd->vclock_mode != VCLOCK_NONE)) {
		if (likely(tv != NULL)) {
			union tstv_t {
				struct timespec ts;
				struct timeval tv;
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
	return vdso_fallback_gettimeofday(tv, tz);
}
int
gettimeofday(struct timeval *, struct timezone *)
	__attribute__((weak, alias("__vdso_gettimeofday")));

notrace int
__vdso_gettimeofday_stick(struct timeval *tv, struct timezone *tz)
{
	struct vvar_data *vvd = get_vvar_data();

	if (likely(vvd->vclock_mode != VCLOCK_NONE)) {
		if (likely(tv != NULL)) {
			union tstv_t {
				struct timespec ts;
				struct timeval tv;
			} *tstv = (union tstv_t *) tv;
			do_realtime_stick(vvd, &tstv->ts);
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
	return vdso_fallback_gettimeofday(tv, tz);
}
