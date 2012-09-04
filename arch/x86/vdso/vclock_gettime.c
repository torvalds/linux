/*
 * Copyright 2006 Andi Kleen, SUSE Labs.
 * Subject to the GNU Public License, v.2
 *
 * Fast user context implementation of clock_gettime, gettimeofday, and time.
 *
 * The code should have no internal unresolved relocations.
 * Check with readelf after changing.
 */

/* Disable profiling for userspace code: */
#define DISABLE_BRANCH_PROFILING

#include <linux/kernel.h>
#include <linux/posix-timers.h>
#include <linux/time.h>
#include <linux/string.h>
#include <asm/vsyscall.h>
#include <asm/fixmap.h>
#include <asm/vgtod.h>
#include <asm/timex.h>
#include <asm/hpet.h>
#include <asm/unistd.h>
#include <asm/io.h>

#define gtod (&VVAR(vsyscall_gtod_data))

notrace static cycle_t vread_tsc(void)
{
	cycle_t ret;
	u64 last;

	/*
	 * Empirically, a fence (of type that depends on the CPU)
	 * before rdtsc is enough to ensure that rdtsc is ordered
	 * with respect to loads.  The various CPU manuals are unclear
	 * as to whether rdtsc can be reordered with later loads,
	 * but no one has ever seen it happen.
	 */
	rdtsc_barrier();
	ret = (cycle_t)vget_cycles();

	last = VVAR(vsyscall_gtod_data).clock.cycle_last;

	if (likely(ret >= last))
		return ret;

	/*
	 * GCC likes to generate cmov here, but this branch is extremely
	 * predictable (it's just a funciton of time and the likely is
	 * very likely) and there's a data dependence, so force GCC
	 * to generate a branch instead.  I don't barrier() because
	 * we don't actually need a barrier, and if this function
	 * ever gets inlined it will generate worse code.
	 */
	asm volatile ("");
	return last;
}

static notrace cycle_t vread_hpet(void)
{
	return readl((const void __iomem *)fix_to_virt(VSYSCALL_HPET) + 0xf0);
}

notrace static long vdso_fallback_gettime(long clock, struct timespec *ts)
{
	long ret;
	asm("syscall" : "=a" (ret) :
	    "0" (__NR_clock_gettime),"D" (clock), "S" (ts) : "memory");
	return ret;
}

notrace static long vdso_fallback_gtod(struct timeval *tv, struct timezone *tz)
{
	long ret;

	asm("syscall" : "=a" (ret) :
	    "0" (__NR_gettimeofday), "D" (tv), "S" (tz) : "memory");
	return ret;
}


notrace static inline u64 vgetsns(void)
{
	long v;
	cycles_t cycles;
	if (gtod->clock.vclock_mode == VCLOCK_TSC)
		cycles = vread_tsc();
	else if (gtod->clock.vclock_mode == VCLOCK_HPET)
		cycles = vread_hpet();
	else
		return 0;
	v = (cycles - gtod->clock.cycle_last) & gtod->clock.mask;
	return v * gtod->clock.mult;
}

/* Code size doesn't matter (vdso is 4k anyway) and this is faster. */
notrace static int __always_inline do_realtime(struct timespec *ts)
{
	unsigned long seq;
	u64 ns;
	int mode;

	ts->tv_nsec = 0;
	do {
		seq = read_seqcount_begin(&gtod->seq);
		mode = gtod->clock.vclock_mode;
		ts->tv_sec = gtod->wall_time_sec;
		ns = gtod->wall_time_snsec;
		ns += vgetsns();
		ns >>= gtod->clock.shift;
	} while (unlikely(read_seqcount_retry(&gtod->seq, seq)));

	timespec_add_ns(ts, ns);
	return mode;
}

notrace static int do_monotonic(struct timespec *ts)
{
	unsigned long seq;
	u64 ns;
	int mode;

	ts->tv_nsec = 0;
	do {
		seq = read_seqcount_begin(&gtod->seq);
		mode = gtod->clock.vclock_mode;
		ts->tv_sec = gtod->monotonic_time_sec;
		ns = gtod->monotonic_time_snsec;
		ns += vgetsns();
		ns >>= gtod->clock.shift;
	} while (unlikely(read_seqcount_retry(&gtod->seq, seq)));
	timespec_add_ns(ts, ns);

	return mode;
}

notrace static int do_realtime_coarse(struct timespec *ts)
{
	unsigned long seq;
	do {
		seq = read_seqcount_begin(&gtod->seq);
		ts->tv_sec = gtod->wall_time_coarse.tv_sec;
		ts->tv_nsec = gtod->wall_time_coarse.tv_nsec;
	} while (unlikely(read_seqcount_retry(&gtod->seq, seq)));
	return 0;
}

notrace static int do_monotonic_coarse(struct timespec *ts)
{
	unsigned long seq;
	do {
		seq = read_seqcount_begin(&gtod->seq);
		ts->tv_sec = gtod->monotonic_time_coarse.tv_sec;
		ts->tv_nsec = gtod->monotonic_time_coarse.tv_nsec;
	} while (unlikely(read_seqcount_retry(&gtod->seq, seq)));

	return 0;
}

notrace int __vdso_clock_gettime(clockid_t clock, struct timespec *ts)
{
	int ret = VCLOCK_NONE;

	switch (clock) {
	case CLOCK_REALTIME:
		ret = do_realtime(ts);
		break;
	case CLOCK_MONOTONIC:
		ret = do_monotonic(ts);
		break;
	case CLOCK_REALTIME_COARSE:
		return do_realtime_coarse(ts);
	case CLOCK_MONOTONIC_COARSE:
		return do_monotonic_coarse(ts);
	}

	if (ret == VCLOCK_NONE)
		return vdso_fallback_gettime(clock, ts);
	return 0;
}
int clock_gettime(clockid_t, struct timespec *)
	__attribute__((weak, alias("__vdso_clock_gettime")));

notrace int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	long ret = VCLOCK_NONE;

	if (likely(tv != NULL)) {
		BUILD_BUG_ON(offsetof(struct timeval, tv_usec) !=
			     offsetof(struct timespec, tv_nsec) ||
			     sizeof(*tv) != sizeof(struct timespec));
		ret = do_realtime((struct timespec *)tv);
		tv->tv_usec /= 1000;
	}
	if (unlikely(tz != NULL)) {
		/* Avoid memcpy. Some old compilers fail to inline it */
		tz->tz_minuteswest = gtod->sys_tz.tz_minuteswest;
		tz->tz_dsttime = gtod->sys_tz.tz_dsttime;
	}

	if (ret == VCLOCK_NONE)
		return vdso_fallback_gtod(tv, tz);
	return 0;
}
int gettimeofday(struct timeval *, struct timezone *)
	__attribute__((weak, alias("__vdso_gettimeofday")));

/*
 * This will break when the xtime seconds get inaccurate, but that is
 * unlikely
 */
notrace time_t __vdso_time(time_t *t)
{
	/* This is atomic on x86_64 so we don't need any locks. */
	time_t result = ACCESS_ONCE(VVAR(vsyscall_gtod_data).wall_time_sec);

	if (t)
		*t = result;
	return result;
}
int time(time_t *t)
	__attribute__((weak, alias("__vdso_time")));
