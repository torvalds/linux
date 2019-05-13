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
extern u8 pvclock_page[PAGE_SIZE]
	__attribute__((visibility("hidden")));
#endif

#ifdef CONFIG_HYPERV_TSCPAGE
extern u8 hvclock_page[PAGE_SIZE]
	__attribute__((visibility("hidden")));
#endif

#ifndef BUILD_VDSO32

notrace static long vdso_fallback_gettime(long clock, struct timespec *ts)
{
	long ret;
	asm ("syscall" : "=a" (ret), "=m" (*ts) :
	     "0" (__NR_clock_gettime), "D" (clock), "S" (ts) :
	     "rcx", "r11");
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
		: "edx");
	return ret;
}

#endif

#ifdef CONFIG_PARAVIRT_CLOCK
static notrace const struct pvclock_vsyscall_time_info *get_pvti0(void)
{
	return (const struct pvclock_vsyscall_time_info *)&pvclock_page;
}

static notrace u64 vread_pvclock(void)
{
	const struct pvclock_vcpu_time_info *pvti = &get_pvti0()->pvti;
	u32 version;
	u64 ret;

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

		if (unlikely(!(pvti->flags & PVCLOCK_TSC_STABLE_BIT)))
			return U64_MAX;

		ret = __pvclock_read_cycles(pvti, rdtsc_ordered());
	} while (pvclock_read_retry(pvti, version));

	return ret;
}
#endif
#ifdef CONFIG_HYPERV_TSCPAGE
static notrace u64 vread_hvclock(void)
{
	const struct ms_hyperv_tsc_page *tsc_pg =
		(const struct ms_hyperv_tsc_page *)&hvclock_page;

	return hv_read_tsc_page(tsc_pg);
}
#endif

notrace static inline u64 vgetcyc(int mode)
{
	if (mode == VCLOCK_TSC)
		return (u64)rdtsc_ordered();
#ifdef CONFIG_PARAVIRT_CLOCK
	else if (mode == VCLOCK_PVCLOCK)
		return vread_pvclock();
#endif
#ifdef CONFIG_HYPERV_TSCPAGE
	else if (mode == VCLOCK_HVCLOCK)
		return vread_hvclock();
#endif
	return U64_MAX;
}

notrace static int do_hres(clockid_t clk, struct timespec *ts)
{
	struct vgtod_ts *base = &gtod->basetime[clk];
	u64 cycles, last, sec, ns;
	unsigned int seq;

	do {
		seq = gtod_read_begin(gtod);
		cycles = vgetcyc(gtod->vclock_mode);
		ns = base->nsec;
		last = gtod->cycle_last;
		if (unlikely((s64)cycles < 0))
			return vdso_fallback_gettime(clk, ts);
		if (cycles > last)
			ns += (cycles - last) * gtod->mult;
		ns >>= gtod->shift;
		sec = base->sec;
	} while (unlikely(gtod_read_retry(gtod, seq)));

	/*
	 * Do this outside the loop: a race inside the loop could result
	 * in __iter_div_u64_rem() being extremely slow.
	 */
	ts->tv_sec = sec + __iter_div_u64_rem(ns, NSEC_PER_SEC, &ns);
	ts->tv_nsec = ns;

	return 0;
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
		return do_hres(clock, ts);
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

		do_hres(CLOCK_REALTIME, ts);
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
