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
	unsigned count;
	unsigned long sec, ns;
	struct vdso_data *vdso = (struct vdso_data *)get_datapage();

	/* The use of the timezone is obsolete, normally tz is NULL. */
	if (unlikely(tz != NULL)) {
		do {
			count = read_seqcount_begin(&vdso->tz_seq);
			tz->tz_minuteswest = vdso->tz_minuteswest;
			tz->tz_dsttime = vdso->tz_dsttime;
		} while (unlikely(read_seqcount_retry(&vdso->tz_seq, count)));
	}

	if (unlikely(tv == NULL))
		return 0;

	do {
		count = read_seqcount_begin(&vdso->tb_seq);
		sec = vdso->xtime_clock_sec;
		cycles = get_cycles() - vdso->xtime_tod_stamp;
		ns = (cycles * vdso->mult) + vdso->xtime_clock_nsec;
		ns >>= vdso->shift;
		if (ns >= NSEC_PER_SEC) {
			ns -= NSEC_PER_SEC;
			sec += 1;
		}
	} while (unlikely(read_seqcount_retry(&vdso->tb_seq, count)));

	tv->tv_sec = sec;
	tv->tv_usec = ns / 1000;

	return 0;
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
	__attribute__((weak, alias("__vdso_gettimeofday")));
