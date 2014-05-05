/*
 * Copyright 2006 Andi Kleen, SUSE Labs.
 * Subject to the GNU Public License, v.2
 *
 * Fast user context implementation of clock_gettime, gettimeofday, and time.
 *
 * 32 Bit compat layer by Stefani Seibold <stefani@seibold.net>
 *  sponsored by Rohde & Schwarz GmbH & Co. KG Munich/Germany
 *
 * The code should have no internal unresolved relocations.
 * Check with readelf after changing.
 */

/* Disable profiling for userspace code: */
#define DISABLE_BRANCH_PROFILING

#include <uapi/linux/time.h>
#include <asm/vgtod.h>
#include <asm/hpet.h>
#include <asm/vvar.h>
#include <asm/unistd.h>
#include <asm/msr.h>
#include <linux/math64.h>
#include <linux/time.h>

#define gtod (&VVAR(vsyscall_gtod_data))

extern int __vdso_clock_gettime(clockid_t clock, struct timespec *ts);
extern int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz);
extern time_t __vdso_time(time_t *t);

#ifdef CONFIG_HPET_TIMER
extern u8 hpet_page
	__attribute__((visibility("hidden")));

static notrace cycle_t vread_hpet(void)
{
	return *(const volatile u32 *)(&hpet_page + HPET_COUNTER);
}
#endif

#ifndef BUILD_VDSO32

#include <linux/kernel.h>
#include <asm/vsyscall.h>
#include <asm/fixmap.h>
#include <asm/pvclock.h>

notrace static long vdso_fallback_gettime(long clock, struct timespec *ts)
{
	long ret;
	asm("syscall" : "=a" (ret) :
	    "0" (__NR_clock_gettime), "D" (clock), "S" (ts) : "memory");
	return ret;
}

notrace static long vdso_fallback_gtod(struct timeval *tv, struct timezone *tz)
{
	long ret;

	asm("syscall" : "=a" (ret) :
	    "0" (__NR_gettimeofday), "D" (tv), "S" (tz) : "memory");
	return ret;
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
	u8 flags;
	unsigned cpu, cpu1;


	/*
	 * Note: hypervisor must guarantee that:
	 * 1. cpu ID number maps 1:1 to per-CPU pvclock time info.
	 * 2. that per-CPU pvclock time info is updated if the
	 *    underlying CPU changes.
	 * 3. that version is increased whenever underlying CPU
	 *    changes.
	 *
	 */
	do {
		cpu = __getcpu() & VGETCPU_CPU_MASK;
		/* TODO: We can put vcpu id into higher bits of pvti.version.
		 * This will save a couple of cycles by getting rid of
		 * __getcpu() calls (Gleb).
		 */

		pvti = get_pvti(cpu);

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
			  pvti->pvti.version != version));

	if (unlikely(!(flags & PVCLOCK_TSC_STABLE_BIT)))
		*mode = VCLOCK_NONE;

	/* refer to tsc.c read_tsc() comment for rationale */
	last = gtod->cycle_last;

	if (likely(ret >= last))
		return ret;

	return last;
}
#endif

#else

notrace static long vdso_fallback_gettime(long clock, struct timespec *ts)
{
	long ret;

	asm(
		"mov %%ebx, %%edx \n"
		"mov %2, %%ebx \n"
		"call __kernel_vsyscall \n"
		"mov %%edx, %%ebx \n"
		: "=a" (ret)
		: "0" (__NR_clock_gettime), "g" (clock), "c" (ts)
		: "memory", "edx");
	return ret;
}

notrace static long vdso_fallback_gtod(struct timeval *tv, struct timezone *tz)
{
	long ret;

	asm(
		"mov %%ebx, %%edx \n"
		"mov %2, %%ebx \n"
		"call __kernel_vsyscall \n"
		"mov %%edx, %%ebx \n"
		: "=a" (ret)
		: "0" (__NR_gettimeofday), "g" (tv), "c" (tz)
		: "memory", "edx");
	return ret;
}

#ifdef CONFIG_PARAVIRT_CLOCK

static notrace cycle_t vread_pvclock(int *mode)
{
	*mode = VCLOCK_NONE;
	return 0;
}
#endif

#endif

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
	ret = (cycle_t)__native_read_tsc();

	last = gtod->cycle_last;

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

notrace static inline u64 vgetsns(int *mode)
{
	u64 v;
	cycles_t cycles;

	if (gtod->vclock_mode == VCLOCK_TSC)
		cycles = vread_tsc();
#ifdef CONFIG_HPET_TIMER
	else if (gtod->vclock_mode == VCLOCK_HPET)
		cycles = vread_hpet();
#endif
#ifdef CONFIG_PARAVIRT_CLOCK
	else if (gtod->vclock_mode == VCLOCK_PVCLOCK)
		cycles = vread_pvclock(mode);
#endif
	else
		return 0;
	v = (cycles - gtod->cycle_last) & gtod->mask;
	return v * gtod->mult;
}

/* Code size doesn't matter (vdso is 4k anyway) and this is faster. */
notrace static int __always_inline do_realtime(struct timespec *ts)
{
	unsigned long seq;
	u64 ns;
	int mode;

	do {
		seq = gtod_read_begin(gtod);
		mode = gtod->vclock_mode;
		ts->tv_sec = gtod->wall_time_sec;
		ns = gtod->wall_time_snsec;
		ns += vgetsns(&mode);
		ns >>= gtod->shift;
	} while (unlikely(gtod_read_retry(gtod, seq)));

	ts->tv_sec += __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return mode;
}

notrace static int __always_inline do_monotonic(struct timespec *ts)
{
	unsigned long seq;
	u64 ns;
	int mode;

	do {
		seq = gtod_read_begin(gtod);
		mode = gtod->vclock_mode;
		ts->tv_sec = gtod->monotonic_time_sec;
		ns = gtod->monotonic_time_snsec;
		ns += vgetsns(&mode);
		ns >>= gtod->shift;
	} while (unlikely(gtod_read_retry(gtod, seq)));

	ts->tv_sec += __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return mode;
}

notrace static void do_realtime_coarse(struct timespec *ts)
{
	unsigned long seq;
	do {
		seq = gtod_read_begin(gtod);
		ts->tv_sec = gtod->wall_time_coarse_sec;
		ts->tv_nsec = gtod->wall_time_coarse_nsec;
	} while (unlikely(gtod_read_retry(gtod, seq)));
}

notrace static void do_monotonic_coarse(struct timespec *ts)
{
	unsigned long seq;
	do {
		seq = gtod_read_begin(gtod);
		ts->tv_sec = gtod->monotonic_time_coarse_sec;
		ts->tv_nsec = gtod->monotonic_time_coarse_nsec;
	} while (unlikely(gtod_read_retry(gtod, seq)));
}

notrace int __vdso_clock_gettime(clockid_t clock, struct timespec *ts)
{
	switch (clock) {
	case CLOCK_REALTIME:
		if (do_realtime(ts) == VCLOCK_NONE)
			goto fallback;
		break;
	case CLOCK_MONOTONIC:
		if (do_monotonic(ts) == VCLOCK_NONE)
			goto fallback;
		break;
	case CLOCK_REALTIME_COARSE:
		do_realtime_coarse(ts);
		break;
	case CLOCK_MONOTONIC_COARSE:
		do_monotonic_coarse(ts);
		break;
	default:
		goto fallback;
	}

	return 0;
fallback:
	return vdso_fallback_gettime(clock, ts);
}
int clock_gettime(clockid_t, struct timespec *)
	__attribute__((weak, alias("__vdso_clock_gettime")));

notrace int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	if (likely(tv != NULL)) {
		if (unlikely(do_realtime((struct timespec *)tv) == VCLOCK_NONE))
			return vdso_fallback_gtod(tv, tz);
		tv->tv_usec /= 1000;
	}
	if (unlikely(tz != NULL)) {
		tz->tz_minuteswest = gtod->tz_minuteswest;
		tz->tz_dsttime = gtod->tz_dsttime;
	}

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
	/* This is atomic on x86 so we don't need any locks. */
	time_t result = ACCESS_ONCE(gtod->wall_time_sec);

	if (t)
		*t = result;
	return result;
}
int time(time_t *t)
	__attribute__((weak, alias("__vdso_time")));
