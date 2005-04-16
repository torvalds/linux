/*
 * arch/sh/mm/cache-sh2.c
 *
 * Copyright (C) 2002 Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#include <linux/init.h>
#include <linux/mm.h>

#include <asm/cache.h>
#include <asm/addrspace.h>
#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/io.h>

/*
 * Calculate the OC address and set the way bit on the SH-2.
 *
 * We must have already jump_to_P2()'ed prior to calling this
 * function, since we rely on CCR manipulation to do the
 * Right Thing(tm).
 */
unsigned long __get_oc_addr(unsigned long set, unsigned long way)
{
	unsigned long ccr;

	/*
	 * On SH-2 the way bit isn't tracked in the address field
	 * if we're doing address array access .. instead, we need
	 * to manually switch out the way in the CCR.
	 */
	ccr = ctrl_inl(CCR);
	ccr &= ~0x00c0;
	ccr |= way << cpu_data->dcache.way_shift;

	/*
	 * Despite the number of sets being halved, we end up losing
	 * the first 2 ways to OCRAM instead of the last 2 (if we're
	 * 4-way). As a result, forcibly setting the W1 bit handily
	 * bumps us up 2 ways.
	 */
	if (ccr & CCR_CACHE_ORA)
		ccr |= 1 << (cpu_data->dcache.way_shift + 1);

	ctrl_outl(ccr, CCR);

	return CACHE_OC_ADDRESS_ARRAY | (set << cpu_data->dcache.entry_shift);
}

