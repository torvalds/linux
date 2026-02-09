/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_TIMEX_H
#define _ASM_TIMEX_H

#ifdef __KERNEL__

#include <linux/compiler.h>

#include <asm/cpu.h>
#include <asm/cpu-features.h>

typedef unsigned long cycles_t;

#define get_cycles get_cycles

static inline cycles_t get_cycles(void)
{
#ifdef CONFIG_32BIT
	return rdtime_l();
#else
	return rdtime_d();
#endif
}

#ifdef CONFIG_32BIT

#define get_cycles_hi get_cycles_hi

static inline cycles_t get_cycles_hi(void)
{
	return rdtime_h();
}

#endif

static inline u64 get_cycles64(void)
{
#ifdef CONFIG_32BIT
	u32 hi, lo;

	do {
		hi = rdtime_h();
		lo = rdtime_l();
	} while (hi != rdtime_h());

	return ((u64)hi << 32) | lo;
#else
	return rdtime_d();
#endif
}

#endif /* __KERNEL__ */

#endif /*  _ASM_TIMEX_H */
