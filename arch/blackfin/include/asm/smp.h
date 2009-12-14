/*
 * Copyright 2007-2009 Analog Devices Inc.
 *                          Philippe Gerum <rpm@xenomai.org>
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_BLACKFIN_SMP_H
#define __ASM_BLACKFIN_SMP_H

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/cpumask.h>
#include <linux/cache.h>
#include <asm/blackfin.h>
#include <mach/smp.h>

#define raw_smp_processor_id()  blackfin_core_id()

extern char coreb_trampoline_start, coreb_trampoline_end;

struct corelock_slot {
	int lock;
};

void smp_icache_flush_range_others(unsigned long start,
				   unsigned long end);

#endif /* !__ASM_BLACKFIN_SMP_H */
