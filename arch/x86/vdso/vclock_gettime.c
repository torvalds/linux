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
#include <asm/pvclock.h>

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
	return readl((const void __iomem *)fix_to_virt(VSYSCALL_HPET) + HPET_COUNTER);
}

#ifdef CONFIG_PARAVIRT_CLOCK

static notrace const struct pvclock_vsyscall_time_info *get_pvti(int cpu)
{
	const struct pvclock_vsyscall_time_info *pvti_base;
	int idx = cpu / (PAGE_SIZE/PVTI_SIZE);
	int offset = cpu % (PAGE_SIZE/PVTI_SIZE);

	BUG_ON(PVCLOCK_FIXMAP_BEGIN + idx > PVCLOCK_FIXMAP_END);

	pvti_base = (struct pvclock_vsyscall_time_info *)
		    __fix_to_virt(PVCLOCK_FIXMAP_BEGIN+idx);

	return &pvti_base[offset];
}

static notrace cycle_t vread_pvclock(int *mode)
{
	const struct pvclock_vsyscall_time_info *pvti;
	cycle_t ret;
	u64 last;
	u32 version;
	u32 migrate_count;
	u8 flags;
	unsigned cpu, cpu1;


	/*
	 * When looping to get a consistent (time-info, tsc) pair, we
	 * also need to deal with the possibility we can switch vcpus,
	 * so make sure we always re-fetch time-info for the current vcpu.
	 */
	do {
		cpu = __getcpu() & VGETCPU_CPU_MASK;
		/* TODO: We can put vcpu id into higher bits of pvti.version.
		 * This will save a couple of cycles by getting rid of
		 * __getcpu() calls (Gleb).
		 */

		pvti = get_pvti(cpu);

		migrate_count = pvti->migrate_count;

		version = __pvclock_read_cycles(&pvti->pvti, &ret, &flags);

		/*
		 * Test we're still on the cpu as well as the version.
		 * We could have been migrated just after the first
		 * vgetcpu but before fetching the version, so we
		 * wouldn't notice a version change.
		 */
		cpu1 = __getcpu() & VGETCPU_CPU_MASK;
	} while (unlikely(cpu != cpu1 ||
			  (pvti->pvti.version & 1) ||
			  pvti->pvti.version != version ||
			  pvti->migrate_count != migrate_count));

	if (unlikely(!(flags & PVCLOCK_TSC_STABLE_BIT)))
		*mode = VCLOCK_NONE;

	/* refer to tsc.c read_tsc() comment for rationale */
	last = VVAR(vsyscall_gtod_data).clock.cycle_last;

	if (likely(ret >= last))
		return ret;

	return last;
}
#endif

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


notrace static inline u64 vgetsns(int *mode)
{
	long v;
	cycles_t cycles;
	if (gtod->clock.vclock_mode == VCLOCK_TSC)
		cycles = vread_tsc();
	else if (gtod->clock.vclock_mode == VCLOCK_HPET)
		cycles = vread_hpet();
#ifdef CONFIG_PARAVIRT_CLOCK
	else if (gtod->clock.vclock_mode == VCLOCK_PVCLOCK)
		cycles = vread_pvclock(mode);
#endif
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
		ns += vgetsns(&mode);
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
		ns += vgetsns(&mode);
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
