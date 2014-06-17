/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#define VDSO_BUILD  /* avoid some shift warnings for -m32 in <asm/page.h> */
#include <linux/time.h>
#include <asm/timex.h>
#include <asm/vdso.h>

#if CHIP_HAS_SPLIT_CYCLE()
static inline cycles_t get_cycles_inline(void)
{
	unsigned int high = __insn_mfspr(SPR_CYCLE_HIGH);
	unsigned int low = __insn_mfspr(SPR_CYCLE_LOW);
	unsigned int high2 = __insn_mfspr(SPR_CYCLE_HIGH);

	while (unlikely(high != high2)) {
		low = __insn_mfspr(SPR_CYCLE_LOW);
		high = high2;
		high2 = __insn_mfspr(SPR_CYCLE_HIGH);
	}

	return (((cycles_t)high) << 32) | low;
}
#define get_cycles get_cycles_inline
#endif

/*
 * Find out the vDSO data page address in the process address space.
 */
inline unsigned long get_datapage(void)
{
	unsigned long ret;

	/* vdso data page located in the 2nd vDSO page. */
	asm volatile ("lnk %0" : "=r"(ret));
	ret &= ~(PAGE_SIZE - 1);
	ret += PAGE_SIZE;

	return ret;
}

int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz)
{
	cycles_t cycles;
	unsigned long count, sec, ns;
	volatile struct vdso_data *vdso_data;

	vdso_data = (struct vdso_data *)get_datapage();
	/* The use of the timezone is obsolete, normally tz is NULL. */
	if (unlikely(tz != NULL)) {
		while (1) {
			/* Spin until the update finish. */
			count = vdso_data->tz_update_count;
			if (count & 1)
				continue;

			tz->tz_minuteswest = vdso_data->tz_minuteswest;
			tz->tz_dsttime = vdso_data->tz_dsttime;

			/* Check whether updated, read again if so. */
			if (count == vdso_data->tz_update_count)
				break;
		}
	}

	if (unlikely(tv == NULL))
		return 0;

	while (1) {
		/* Spin until the update finish. */
		count = vdso_data->tb_update_count;
		if (count & 1)
			continue;

		cycles = (get_cycles() - vdso_data->xtime_tod_stamp);
		ns = (cycles * vdso_data->mult) >> vdso_data->shift;
		sec = vdso_data->xtime_clock_sec;
		ns += vdso_data->xtime_clock_nsec;
		if (ns >= NSEC_PER_SEC) {
			ns -= NSEC_PER_SEC;
			sec += 1;
		}

		/* Check whether updated, read again if so. */
		if (count == vdso_data->tb_update_count)
			break;
	}

	tv->tv_sec = sec;
	tv->tv_usec = ns / 1000;

	return 0;
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
	__attribute__((weak, alias("__vdso_gettimeofday")));
