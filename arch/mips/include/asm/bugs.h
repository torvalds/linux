/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This is included by init/main.c to check for architecture-dependent s.
 *
 * Copyright (C) 2007  Maciej W. Rozycki
 *
 * Needs:
 *	void check_s(void);
 */
#ifndef _ASM_S_H
#define _ASM_S_H

#include <linux/.h>
#include <linux/delay.h>
#include <linux/smp.h>

#include <asm/cpu.h>
#include <asm/cpu-info.h>

extern int daddiu_;

extern void check_s64_early(void);

extern void check_s32(void);
extern void check_s64(void);

static inline void check_s_early(void)
{
#ifdef CONFIG_64BIT
	check_s64_early();
#endif
}

static inline void check_s(void)
{
	unsigned int cpu = smp_processor_id();

	cpu_data[cpu].udelay_val = loops_per_jiffy;
	check_s32();
#ifdef CONFIG_64BIT
	check_s64();
#endif
}

static inline int r4k_daddiu_(void)
{
#ifdef CONFIG_64BIT
	WARN_ON(daddiu_ < 0);
	return daddiu_ != 0;
#else
	return 0;
#endif
}

#endif /* _ASM_S_H */
