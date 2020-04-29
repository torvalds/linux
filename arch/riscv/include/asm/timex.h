/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_TIMEX_H
#define _ASM_RISCV_TIMEX_H

#include <asm/csr.h>
#include <asm/mmio.h>

typedef unsigned long cycles_t;

extern u64 __iomem *riscv_time_val;
extern u64 __iomem *riscv_time_cmp;

#ifdef CONFIG_64BIT
#define mmio_get_cycles()	readq_relaxed(riscv_time_val)
#else
#define mmio_get_cycles()	readl_relaxed(riscv_time_val)
#define mmio_get_cycles_hi()	readl_relaxed(((u32 *)riscv_time_val) + 1)
#endif

static inline cycles_t get_cycles(void)
{
	if (IS_ENABLED(CONFIG_RISCV_SBI))
		return csr_read(CSR_TIME);
	return mmio_get_cycles();
}
#define get_cycles get_cycles

#ifdef CONFIG_64BIT
static inline u64 get_cycles64(void)
{
	return get_cycles();
}
#else /* CONFIG_64BIT */
static inline u32 get_cycles_hi(void)
{
	if (IS_ENABLED(CONFIG_RISCV_SBI))
		return csr_read(CSR_TIMEH);
	return mmio_get_cycles_hi();
}

static inline u64 get_cycles64(void)
{
	u32 hi, lo;

	do {
		hi = get_cycles_hi();
		lo = get_cycles();
	} while (hi != get_cycles_hi());

	return ((u64)hi << 32) | lo;
}
#endif /* CONFIG_64BIT */

#define ARCH_HAS_READ_CURRENT_TIMER
static inline int read_current_timer(unsigned long *timer_val)
{
	*timer_val = get_cycles();
	return 0;
}

#endif /* _ASM_RISCV_TIMEX_H */
