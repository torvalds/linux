/* SMP global caching code
 *
 * Copyright (C) 2010 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/threads.h>
#include <linux/interrupt.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/smp.h>
#include "cache-smp.h"

DEFINE_SPINLOCK(smp_cache_lock);
static unsigned long smp_cache_mask;
static unsigned long smp_cache_start;
static unsigned long smp_cache_end;
static cpumask_t smp_cache_ipi_map;		/* Bitmask of cache IPI done CPUs */

/**
 * smp_cache_interrupt - Handle IPI request to flush caches.
 *
 * Handle a request delivered by IPI to flush the current CPU's
 * caches.  The parameters are stored in smp_cache_*.
 */
void smp_cache_interrupt(void)
{
	unsigned long opr_mask = smp_cache_mask;

	switch ((enum smp_dcache_ops)(opr_mask & SMP_DCACHE_OP_MASK)) {
	case SMP_DCACHE_NOP:
		break;
	case SMP_DCACHE_INV:
		mn10300_local_dcache_inv();
		break;
	case SMP_DCACHE_INV_RANGE:
		mn10300_local_dcache_inv_range(smp_cache_start, smp_cache_end);
		break;
	case SMP_DCACHE_FLUSH:
		mn10300_local_dcache_flush();
		break;
	case SMP_DCACHE_FLUSH_RANGE:
		mn10300_local_dcache_flush_range(smp_cache_start,
						 smp_cache_end);
		break;
	case SMP_DCACHE_FLUSH_INV:
		mn10300_local_dcache_flush_inv();
		break;
	case SMP_DCACHE_FLUSH_INV_RANGE:
		mn10300_local_dcache_flush_inv_range(smp_cache_start,
						     smp_cache_end);
		break;
	}

	switch ((enum smp_icache_ops)(opr_mask & SMP_ICACHE_OP_MASK)) {
	case SMP_ICACHE_NOP:
		break;
	case SMP_ICACHE_INV:
		mn10300_local_icache_inv();
		break;
	case SMP_ICACHE_INV_RANGE:
		mn10300_local_icache_inv_range(smp_cache_start, smp_cache_end);
		break;
	}

	cpu_clear(smp_processor_id(), smp_cache_ipi_map);
}

/**
 * smp_cache_call - Issue an IPI to request the other CPUs flush caches
 * @opr_mask: Cache operation flags
 * @start: Start address of request
 * @end: End address of request
 *
 * Send cache flush IPI to other CPUs.  This invokes smp_cache_interrupt()
 * above on those other CPUs and then waits for them to finish.
 *
 * The caller must hold smp_cache_lock.
 */
void smp_cache_call(unsigned long opr_mask,
		    unsigned long start, unsigned long end)
{
	smp_cache_mask = opr_mask;
	smp_cache_start = start;
	smp_cache_end = end;
	smp_cache_ipi_map = cpu_online_map;
	cpu_clear(smp_processor_id(), smp_cache_ipi_map);

	send_IPI_allbutself(FLUSH_CACHE_IPI);

	while (!cpus_empty(smp_cache_ipi_map))
		/* nothing. lockup detection does not belong here */
		mb();
}
