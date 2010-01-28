/*
 * OMAP4 machine specific smp.h
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
 *
 * Author:
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * Interface functions needed for the SMP. This file is based on arm
 * realview smp platform.
 * Copyright (c) 2003 ARM Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef OMAP_ARCH_SMP_H
#define OMAP_ARCH_SMP_H

#include <asm/hardware/gic.h>

/*
 * set_event() is used to wake up secondary core from wfe using sev. ROM
 * code puts the second core into wfe(standby).
 *
 */
#define set_event()	__asm__ __volatile__ ("sev" : : : "memory")

/* Needed for secondary core boot */
extern void omap_secondary_startup(void);
extern u32 omap_modify_auxcoreboot0(u32 set_mask, u32 clear_mask);
extern void omap_auxcoreboot_addr(u32 cpu_addr);

/*
 * We use Soft IRQ1 as the IPI
 */
static inline void smp_cross_call(const struct cpumask *mask)
{
	gic_raise_softirq(mask, 1);
}

/*
 * Read MPIDR: Multiprocessor affinity register
 */
#define hard_smp_processor_id()			\
	({						\
		unsigned int cpunum;			\
		__asm__("mrc p15, 0, %0, c0, c0, 5"	\
			: "=r" (cpunum));		\
		cpunum &= 0x0F;				\
	})

#endif
