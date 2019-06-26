// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2015 Mentor Graphics Corporation.
 */

#include <linux/compiler.h>
#include <linux/hrtimer.h>
#include <linux/time.h>
#include <asm/barrier.h>
#include <asm/bug.h>
#include <asm/cp15.h>
#include <asm/page.h>
#include <asm/unistd.h>
#include <asm/vdso_datapage.h>

#ifndef CONFIG_AEABI
#error This code depends on AEABI system call conventions
#endif

extern struct vdso_data *__get_datapage(void);

static notrace u32 __vdso_read_begin(const struct vdso_data *vdata)
{
	u32 seq;
repeat:
	seq = READ_ONCE(vdata->seq_count);
	if (seq & 1) {
		cpu_relax();
		goto repeat;
	}
	return seq;
}

static notrace u32 vdso_read_begin(const struct vdso_data *vdata)
{
	u32 seq;

	seq = __vdso_read_begin(vdata);

	smp_rmb(); /* Pairs with smp_wmb in vdso_write_end */
	return seq;
}

static notrace int vdso_read_retry(const struct vdso_data *vdata, u32 start)
{
	smp_rmb(); /* Pairs with smp_wmb in vdso_write_begin */
	return vdata->seq_count != start;
}

static notrace long clock_gettime_fallback(clockid_t _clkid,
					   struct timespec *_ts)
{
	register struct timespec *ts asm("r1") = _ts;
	register clockid_t clkid asm("r0") = _clkid;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_clock_gettime;

	asm volatile(
	"	swi #0\n"
	: "=r" (ret)
	: "r" (clkid), "r" (ts), "r" (nr)
	: "memory");

	return ret;
}

static notrace int do_realtime_coarse(struct timespec *ts,
				      struct vdso_data *vdata)
{
	u32 seq;

	do {
		seq = vdso_read_begin(vdata);

		ts->tv_sec = vdata->xtime_coarse_sec;
		ts->tv_nsec = vdata->xtime_coarse_nsec;

	} while (vdso_read_retry(vdata, seq));

	return 0;
}

static notrace int do_monotonic_coarse(struct timespec *ts,
				       struct vdso_data *vdata)
{
	struct timespec tomono;
	u32 seq;

	do {
		seq = vdso_read_begin(vdata);

		ts->tv_sec = vdata->xtime_coarse_sec;
		ts->tv_nsec = vdata->xtime_coarse_nsec;

		tomono.tv_sec = vdata->wtm_clock_sec;
		tomono.tv_nsec = vdata->wtm_clock_nsec;

	} while (vdso_read_retry(vdata, seq));

	ts->tv_sec += tomono.tv_sec;
	timespec_add_ns(ts, tomono.tv_nsec);

	return 0;
}

#ifdef CONFIG_ARM_ARCH_TIMER

static notrace u64 get_ns(struct vdso_data *vdata)
{
	u64 cycle_delta;
	u64 cycle_now;
	u64 nsec;

	isb();
	cycle_now = read_sysreg(CNTVCT);

	cycle_delta = (cycle_now - vdata->cs_cycle_last) & vdata->cs_mask;

	nsec = (cycle_delta * vdata->cs_mult) + vdata->xtime_clock_snsec;
	nsec >>= vdata->cs_shift;

	return nsec;
}

static notrace int do_realtime(struct timespec *ts, struct vdso_data *vdata)
{
	u64 nsecs;
	u32 seq;

	do {
		seq = vdso_read_begin(vdata);

		if (!vdata->tk_is_cntvct)
			return -1;

		ts->tv_sec = vdata->xtime_clock_sec;
		nsecs = get_ns(vdata);

	} while (vdso_read_retry(vdata, seq));

	ts->tv_nsec = 0;
	timespec_add_ns(ts, nsecs);

	return 0;
}

static notrace int do_monotonic(struct timespec *ts, struct vdso_data *vdata)
{
	struct timespec tomono;
	u64 nsecs;
	u32 seq;

	do {
		seq = vdso_read_begin(vdata);

		if (!vdata->tk_is_cntvct)
			return -1;

		ts->tv_sec = vdata->xtime_clock_sec;
		nsecs = get_ns(vdata);

		tomono.tv_sec = vdata->wtm_clock_sec;
		tomono.tv_nsec = vdata->wtm_clock_nsec;

	} while (vdso_read_retry(vdata, seq));

	ts->tv_sec += tomono.tv_sec;
	ts->tv_nsec = 0;
	timespec_add_ns(ts, nsecs + tomono.tv_nsec);

	return 0;
}

#else /* CONFIG_ARM_ARCH_TIMER */

static notrace int do_realtime(struct timespec *ts, struct vdso_data *vdata)
{
	return -1;
}

static notrace int do_monotonic(struct timespec *ts, struct vdso_data *vdata)
{
	return -1;
}

#endif /* CONFIG_ARM_ARCH_TIMER */

notrace int __vdso_clock_gettime(clockid_t clkid, struct timespec *ts)
{
	struct vdso_data *vdata;
	int ret = -1;

	vdata = __get_datapage();

	switch (clkid) {
	case CLOCK_REALTIME_COARSE:
		ret = do_realtime_coarse(ts, vdata);
		break;
	case CLOCK_MONOTONIC_COARSE:
		ret = do_monotonic_coarse(ts, vdata);
		break;
	case CLOCK_REALTIME:
		ret = do_realtime(ts, vdata);
		break;
	case CLOCK_MONOTONIC:
		ret = do_monotonic(ts, vdata);
		break;
	default:
		break;
	}

	if (ret)
		ret = clock_gettime_fallback(clkid, ts);

	return ret;
}

static notrace long gettimeofday_fallback(struct timeval *_tv,
					  struct timezone *_tz)
{
	register struct timezone *tz asm("r1") = _tz;
	register struct timeval *tv asm("r0") = _tv;
	register long ret asm ("r0");
	register long nr asm("r7") = __NR_gettimeofday;

	asm volatile(
	"	swi #0\n"
	: "=r" (ret)
	: "r" (tv), "r" (tz), "r" (nr)
	: "memory");

	return ret;
}

notrace int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	struct timespec ts;
	struct vdso_data *vdata;
	int ret;

	vdata = __get_datapage();

	ret = do_realtime(&ts, vdata);
	if (ret)
		return gettimeofday_fallback(tv, tz);

	if (tv) {
		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = ts.tv_nsec / 1000;
	}
	if (tz) {
		tz->tz_minuteswest = vdata->tz_minuteswest;
		tz->tz_dsttime = vdata->tz_dsttime;
	}

	return ret;
}

/* Avoid unresolved references emitted by GCC */

void __aeabi_unwind_cpp_pr0(void)
{
}

void __aeabi_unwind_cpp_pr1(void)
{
}

void __aeabi_unwind_cpp_pr2(void)
{
}
