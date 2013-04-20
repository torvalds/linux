/*
 * Meta cache partition manipulation.
 *
 * Copyright 2010 Imagination Technologies Ltd.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <asm/processor.h>
#include <asm/cachepart.h>
#include <asm/metag_isa.h>
#include <asm/metag_mem.h>

#define SYSC_DCPART(n)	(SYSC_DCPART0 + SYSC_xCPARTn_STRIDE * (n))
#define SYSC_ICPART(n)	(SYSC_ICPART0 + SYSC_xCPARTn_STRIDE * (n))

#define CACHE_ASSOCIATIVITY 4 /* 4 way set-assosiative */
#define ICACHE 0
#define DCACHE 1

/* The CORE_CONFIG2 register is not available on Meta 1 */
#ifdef CONFIG_METAG_META21
unsigned int get_dcache_size(void)
{
	unsigned int config2 = metag_in32(METAC_CORE_CONFIG2);
	return 0x1000 << ((config2 & METAC_CORECFG2_DCSZ_BITS)
				>> METAC_CORECFG2_DCSZ_S);
}

unsigned int get_icache_size(void)
{
	unsigned int config2 = metag_in32(METAC_CORE_CONFIG2);
	return 0x1000 << ((config2 & METAC_CORE_C2ICSZ_BITS)
				>> METAC_CORE_C2ICSZ_S);
}

unsigned int get_global_dcache_size(void)
{
	unsigned int cpart = metag_in32(SYSC_DCPART(hard_processor_id()));
	unsigned int temp = cpart & SYSC_xCPARTG_AND_BITS;
	return (get_dcache_size() * ((temp >> SYSC_xCPARTG_AND_S) + 1)) >> 4;
}

unsigned int get_global_icache_size(void)
{
	unsigned int cpart = metag_in32(SYSC_ICPART(hard_processor_id()));
	unsigned int temp = cpart & SYSC_xCPARTG_AND_BITS;
	return (get_icache_size() * ((temp >> SYSC_xCPARTG_AND_S) + 1)) >> 4;
}

static unsigned int get_thread_cache_size(unsigned int cache, int thread_id)
{
	unsigned int cache_size;
	unsigned int t_cache_part;
	unsigned int isEnabled;
	unsigned int offset = 0;
	isEnabled = (cache == DCACHE ? metag_in32(MMCU_DCACHE_CTRL_ADDR) & 0x1 :
		metag_in32(MMCU_ICACHE_CTRL_ADDR) & 0x1);
	if (!isEnabled)
		return 0;
#if PAGE_OFFSET >= LINGLOBAL_BASE
	/* Checking for global cache */
	cache_size = (cache == DCACHE ? get_global_dache_size() :
		get_global_icache_size());
	offset = 8;
#else
	cache_size = (cache == DCACHE ? get_dcache_size() :
		get_icache_size());
#endif
	t_cache_part = (cache == DCACHE ?
		(metag_in32(SYSC_DCPART(thread_id)) >> offset) & 0xF :
		(metag_in32(SYSC_ICPART(thread_id)) >> offset) & 0xF);
	switch (t_cache_part) {
	case 0xF:
		return cache_size;
	case 0x7:
		return cache_size / 2;
	case 0x3:
		return cache_size / 4;
	case 0x1:
		return cache_size / 8;
	case 0:
		return cache_size / 16;
	}
	return -1;
}

void check_for_cache_aliasing(int thread_id)
{
	unsigned int thread_cache_size;
	unsigned int cache_type;
	for (cache_type = ICACHE; cache_type <= DCACHE; cache_type++) {
		thread_cache_size =
				get_thread_cache_size(cache_type, thread_id);
		if (thread_cache_size < 0)
			pr_emerg("Can't read %s cache size", \
				 cache_type ? "DCACHE" : "ICACHE");
		else if (thread_cache_size == 0)
			/* Cache is off. No need to check for aliasing */
			continue;
		if (thread_cache_size / CACHE_ASSOCIATIVITY > PAGE_SIZE) {
			pr_emerg("Cache aliasing detected in %s on Thread %d",
				 cache_type ? "DCACHE" : "ICACHE", thread_id);
			pr_warn("Total %s size: %u bytes",
				cache_type ? "DCACHE" : "ICACHE ",
				cache_type ? get_dcache_size()
				: get_icache_size());
			pr_warn("Thread %s size: %d bytes",
				cache_type ? "CACHE" : "ICACHE",
				thread_cache_size);
			pr_warn("Page Size: %lu bytes", PAGE_SIZE);
		}
	}
}

#else

void check_for_cache_aliasing(int thread_id)
{
	return;
}

#endif
