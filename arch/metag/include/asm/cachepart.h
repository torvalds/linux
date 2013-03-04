/*
 * Meta cache partition manipulation.
 *
 * Copyright 2010 Imagination Technologies Ltd.
 */

#ifndef _METAG_CACHEPART_H_
#define _METAG_CACHEPART_H_

/**
 * get_dcache_size() - Get size of data cache.
 */
unsigned int get_dcache_size(void);

/**
 * get_icache_size() - Get size of code cache.
 */
unsigned int get_icache_size(void);

/**
 * get_global_dcache_size() - Get the thread's global dcache.
 *
 * Returns the size of the current thread's global dcache partition.
 */
unsigned int get_global_dcache_size(void);

/**
 * get_global_icache_size() - Get the thread's global icache.
 *
 * Returns the size of the current thread's global icache partition.
 */
unsigned int get_global_icache_size(void);

/**
 * check_for_dache_aliasing() - Ensure that the bootloader has configured the
 * dache and icache properly to avoid aliasing
 * @thread_id: Hardware thread ID
 *
 */
void check_for_cache_aliasing(int thread_id);

#endif
