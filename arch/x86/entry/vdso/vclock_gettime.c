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

#include <uapi/linux/time.h>
#include <asm/vgtod.h>
#include <asm/vvar.h>
#include <asm/unistd.h>
#include <asm/msr.h>
#include <asm/pvclock.h>
#include <asm/mshyperv.h>
#include <linux/math64.h>
#include <linux/time.h>
#include <linux/kernel.h>

#define gtod (&VVAR(vsyscall_gtod_data))

extern int __vdso_clock_gettime(clockid_t clock, struct timespec *ts);
extern int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz);
extern time_t __vdso_time(time_t *t);

#ifdef CONFIG_PARAVIRT_CLOCK
extern u8 pvclock_page
	__attribute__((visibility("hidden")));
#endif

#ifdef CONFIG_HYPERV_TSCPAGE
extern u8 hvclock_page
	__attribute__((visibility("hidden")));
#endif

#ifndef BUILD_VDSO32

notrace static long vdso_fallback_gettime(long clock, struct timespec *ts)
{
	long ret;
	asm ("syscall" : "=a" (ret), "=m" (*ts) :
	     "0" (__NR_clock_gettime), "D" (clock), "S" (ts) :
	     "memory", "rcx", "r11");
	return ret;
}

notrace static long vdso_fallback_gtod(struct timeval *tv, struct timezone *tz)
{
	long ret;

	asm ("syscall" : "=a" (ret), "=m" (*tv), "=m" (*tz) :
	     "0" (__NR_gettimeofday), "D" (tv), "S" (tz) :
	     "memory", "rcx", "r11");
	return ret;
}


#else

notrace static long vdso_fallback_gettime(long clock, struct timespec *ts)
{
	long ret;

	asm (
		"mov %%ebx, %%edx \n"
		"mov %[clock], %%ebx \n"
		"call __kernel_vsyscall \n"
		"mov %%edx, %%ebx \n"
		: "=a" (ret), "=m" (*ts)
		: "0" (__NR_clock_gettime), [clock] "g" (clock), "c" (ts)
		: "memory", "edx");
	return ret;
}

notrace static long vdso_fallback_gtod(struct timeval *tv, struct timezone *tz)
{
	long ret;

	asm (
		"mov %%ebx, %%edx \n"
		"mov %[tv], %%ebx \n"
		"call __kernel_vsyscall \n"
		"mov %%edx, %%ebx \n"
		: "=a" (ret), "=m" (*tv), "=m" (*tz)
		: "0" (__NR_gettimeofday), [tv] "g" (tv), "c" (tz)
		: "memory", "edx");
	return ret;
}

#endif

#ifdef CONFIG_PARAVIRT_CLOCK
static notrace const struct pvclock_vsyscall_time_info *get_pvti0(void)
{
	return (const struct pvclock_vsyscall_time_info *)&pvclock_page;
}

static notrace u64 vread_pvclock(int *mode)
{
	const struct pvclock_vcpu_time_info *pvti = &get_pvti0()->pvti;
	u64 ret;
	u64 last;
	u32 version;

	/*
	 * Note: The kernel and hypervisor must guarantee that cpu ID
	 * number maps 1:1 to per-CPU pvclock time info.
	 *
	 * Because the hypervisor is entirely unaware of guest userspace
	 * preemption, it cannot guarantee that per-CPU pvclock time
	 * info is updated if the underlying CPU changes or that that
	 * version is increased whenever underlying CPU changes.
	 *
	 * On KVM, we are guaranteed that pvti updates for any vCPU are
	 * atomic as seen by *all* vCPUs.  This is an even stronger
	 * guarantee than we get with a normal seqlock.
	 *
	 * On Xen, we don't appear to have that guarantee, but Xen still
	 * supplies a valid seqlock using the version field.
	 *
	 * We only do pvclock vdso timing at all if
	 * PVCLOCK_TSC_STABLE_BIT is set, and we interpret that bit to
	 * mean that all vCPUs have matching pvti and that the TSC is
	 * synced, so we can just look at vCPU 0's pvti.
	 */

	do {
		version = pvclock_read_begin(pvti);

		if (unlikely(!(pvti->flags & PVCLOCK_TSC_STABLE_BIT))) {
			*mode = VCLOCK_NONE;
			return 0;
		}

		ret = __pvclock_read_cycles(pvti, rdtsc_ordered());
	} while (pvclock_read_retry(pvti, version));

	/* refer to vread_tsc() comment for rationale */
	last = gtod->cycle_last;

	if (likely(ret >= last))
		return ret;

	return last;
}
#endif
#ifdef CONFIG_HYPERV_TSCPAGE
static notrace u64 vread_hvclock(int *mode)
{
	const struct ms_hyperv_tsc_page *tsc_pg =
		(const struct ms_hyperv_tsc_page *)&hvclock_page;
	u64 current_tick = hv_read_tsc_page(tsc_pg);

	if (current_tick != U64_MAX)
		return current_tick;

	*mode = VCLOCK_NONE;
	return 0;
}
#endif

notrace static u64 vread_tsc(void)
{
	u64 ret = (u64)rdtsc_ordered();
	u64 last = gtod->cycle_last;

	if (likely(ret >= last))
		return ret;

	/*
	 * GCC likes to generate cmov here, but this branch is extremely
	 * predictable (it's just a function of time and the likely is
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
#ifdef CONFIG_PARAVIRT_CLOCK
	else if (gtod->vclock_mode == VCLOCK_PVCLOCK)
		cycles = vread_pvclock(mode);
#endif
#ifdef CONFIG_HYPERV_TSCPAGE
	else if (gtod->vclock_mode == VCLOCK_HVCLOCK)
		cycles = vread_hvclock(mode);
#endif
	else
		return 0;
	v = cycles - gtod->cycle_last;
	return v * gtod->mult;
}

notrace static int do_hres(clockid_t clk, struct timespec *ts)
{
	struct vgtod_ts *base = &gtod->basetime[clk];
	unsigned int seq;
	int mode;
	u64 ns;

	do {
		seq = gtod_read_begin(gtod);
		mode = gtod->vclock_mode;
		ts->tv_sec = base->sec;
		ns = base->nsec;
		ns += vgetsns(&mode);
		ns >>= gtod->shift;
	} while (unlikely(gtod_read_retry(gtod, seq)));

	ts->tv_sec += __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return mode;
}

notrace static void do_coarse(clockid_t clk, struct timespec *ts)
{
	struct vgtod_ts *base = &gtod->basetime[clk];
	unsigned int seq;

	do {
		seq = gtod_read_begin(gtod);
		ts->tv_sec = base->sec;
		ts->tv_nsec = base->nsec;
	} while (unlikely(gtod_read_retry(gtod, seq)));
}

notrace int __vdso_clock_gettime(clockid_t clock, struct timespec *ts)
{
	unsigned int msk;

	/* Sort out negative (CPU/FD) and invalid clocks */
	if (unlikely((unsigned int) clock >= MAX_CLOCKS))
		return vdso_fallback_gettime(clock, ts);

	/*
	 * Convert the clockid to a bitmask and use it to check which
	 * clocks are handled in the VDSO directly.
	 */
	msk = 1U << clock;
	if (likely(msk & VGTOD_HRES)) {
		if (do_hres(clock, ts) != VCLOCK_NONE)
			return 0;
	} else if (msk & VGTOD_COARSE) {
		do_coarse(clock, ts);
		return 0;
	}
	return vdso_fallback_gettime(clock, ts);
}

int clock_gettime(clockid_t, struct timespec *)
	__attribute__((weak, alias("__vdso_clock_gettime")));

notrace int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	if (likely(tv != NULL)) {
		struct timespec *ts = (struct timespec *) tv;

		if (unlikely(do_hres(CLOCK_REALTIME, ts) == VCLOCK_NONE))
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
	time_t result = READ_ONCE(gtod->basetime[CLOCK_REALTIME].sec);

	if (t)
		*t = result;
	return result;
}
time_t time(time_t *t)
	__attribute__((weak, alias("__vdso_time")));
