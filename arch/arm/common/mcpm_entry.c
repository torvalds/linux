/*
 * arch/arm/common/mcpm_entry.c -- entry point for multi-cluster PM
 *
 * Created by:  Nicolas Pitre, March 2012
 * Copyright:   (C) 2012-2013  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/mcpm.h>
#include <asm/cacheflush.h>

extern unsigned long mcpm_entry_vectors[MAX_NR_CLUSTERS][MAX_CPUS_PER_CLUSTER];

void mcpm_set_entry_vector(unsigned cpu, unsigned cluster, void *ptr)
{
	unsigned long val = ptr ? virt_to_phys(ptr) : 0;
	mcpm_entry_vectors[cluster][cpu] = val;
	sync_cache_w(&mcpm_entry_vectors[cluster][cpu]);
}
