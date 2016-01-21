/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include "vdso.h"

#include <linux/compiler.h>
#include <linux/irqchip/mips-gic.h>
#include <linux/time.h>

#include <asm/clocksource.h>
#include <asm/io.h>
#include <asm/mips-cm.h>
#include <asm/unistd.h>
#include <asm/vdso.h>

static __always_inline int do_realtime_coarse(struct timespec *ts,
					      const union mips_vdso_data *data)
{
	u32 start_seq;

	do {
		start_seq = vdso_data_read_begin(data);

		ts->tv_sec = data->xtime_sec;
		ts->tv_nsec = data->xtime_nsec >> data->cs_shift;
	} while (vdso_data_read_retry(data, start_seq));

	return 0;
}

static __always_inline int do_monotonic_coarse(struct timespec *ts,
					       const union mips_vdso_data *data)
{
	u32 start_seq;
	u32 to_mono_sec;
	u32 to_mono_nsec;

	do {
		start_seq = vdso_data_read_begin(data);

		ts->tv_sec = data->xtime_sec;
		ts->tv_nsec = data->xtime_nsec >> data->cs_shift;

		to_mono_sec = data->wall_to_mono_sec;
		to_mono_nsec = data->wall_to_mono_nsec;
	} while (vdso_data_read_retry(data, start_seq));

	ts->tv_sec += to_mono_sec;
	timespec_add_ns(ts, to_mono_nsec);

	return 0;
}

#ifdef CONFIG_CSRC_R4K

static __always_inline u64 read_r4k_count(void)
{
	unsigned int count;

	__asm__ __volatile__(
	"	.set push\n"
	"	.set mips32r2\n"
	"	rdhwr	%0, $2\n"
	"	.set pop\n"
	: "=r" (count));

	return count;
}

#endif

#ifdef CONFIG_CLKSRC_MIPS_GIC

static __always_inline u64 read_gic_count(const union mips_vdso_data *data)
{
	void __iomem *gic = get_gic(data);
	u32 hi, hi2, lo;

	do {
		hi = __raw_readl(gic + GIC_UMV_SH_COUNTER_63_32_OFS);
		lo = __raw_readl(gic + GIC_UMV_SH_COUNTER_31_00_OFS);
		hi2 = __raw_readl(gic + GIC_UMV_SH_COUNTER_63_32_OFS);
	} while (hi2 != hi);

	return (((u64)hi) << 32) + lo;
}

#endif

static __always_inline u64 get_ns(const union mips_vdso_data *data)
{
	u64 cycle_now, delta, nsec;

	switch (data->clock_mode) {
#ifdef CONFIG_CSRC_R4K
	case VDSO_CLOCK_R4K:
		cycle_now = read_r4k_count();
		break;
#endif
#ifdef CONFIG_CLKSRC_MIPS_GIC
	case VDSO_CLOCK_GIC:
		cycle_now = read_gic_count(data);
		break;
#endif
	default:
		return 0;
	}

	delta = (cycle_now - data->cs_cycle_last) & data->cs_mask;

	nsec = (delta * data->cs_mult) + data->xtime_nsec;
	nsec >>= data->cs_shift;

	return nsec;
}

static __always_inline int do_realtime(struct timespec *ts,
				       const union mips_vdso_data *data)
{
	u32 start_seq;
	u64 ns;

	do {
		start_seq = vdso_data_read_begin(data);

		if (data->clock_mode == VDSO_CLOCK_NONE)
			return -ENOSYS;

		ts->tv_sec = data->xtime_sec;
		ns = get_ns(data);
	} while (vdso_data_read_retry(data, start_seq));

	ts->tv_nsec = 0;
	timespec_add_ns(ts, ns);

	return 0;
}

static __always_inline int do_monotonic(struct timespec *ts,
					const union mips_vdso_data *data)
{
	u32 start_seq;
	u64 ns;
	u32 to_mono_sec;
	u32 to_mono_nsec;

	do {
		start_seq = vdso_data_read_begin(data);

		if (data->clock_mode == VDSO_CLOCK_NONE)
			return -ENOSYS;

		ts->tv_sec = data->xtime_sec;
		ns = get_ns(data);

		to_mono_sec = data->wall_to_mono_sec;
		to_mono_nsec = data->wall_to_mono_nsec;
	} while (vdso_data_read_retry(data, start_seq));

	ts->tv_sec += to_mono_sec;
	ts->tv_nsec = 0;
	timespec_add_ns(ts, ns + to_mono_nsec);

	return 0;
}

#ifdef CONFIG_MIPS_CLOCK_VSYSCALL

/*
 * This is behind the ifdef so that we don't provide the symbol when there's no
 * possibility of there being a usable clocksource, because there's nothing we
 * can do without it. When libc fails the symbol lookup it should fall back on
 * the standard syscall path.
 */
int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	const union mips_vdso_data *data = get_vdso_data();
	struct timespec ts;
	int ret;

	ret = do_realtime(&ts, data);
	if (ret)
		return ret;

	if (tv) {
		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = ts.tv_nsec / 1000;
	}

	if (tz) {
		tz->tz_minuteswest = data->tz_minuteswest;
		tz->tz_dsttime = data->tz_dsttime;
	}

	return 0;
}

#endif /* CONFIG_CLKSRC_MIPS_GIC */

int __vdso_clock_gettime(clockid_t clkid, struct timespec *ts)
{
	const union mips_vdso_data *data = get_vdso_data();
	int ret;

	switch (clkid) {
	case CLOCK_REALTIME_COARSE:
		ret = do_realtime_coarse(ts, data);
		break;
	case CLOCK_MONOTONIC_COARSE:
		ret = do_monotonic_coarse(ts, data);
		break;
	case CLOCK_REALTIME:
		ret = do_realtime(ts, data);
		break;
	case CLOCK_MONOTONIC:
		ret = do_monotonic(ts, data);
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	/* If we return -ENOSYS libc should fall back to a syscall. */
	return ret;
}
