/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_CACHEFLUSH_H
#define _ASM_TILE_CACHEFLUSH_H

#include <arch/chip.h>

/* Keep includes the same across arches.  */
#include <linux/mm.h>
#include <linux/cache.h>
#include <arch/icache.h>

/* Caches are physically-indexed and so don't need special treatment */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_dup_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr, pfn)	do { } while (0)
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 0
#define flush_dcache_page(page)			do { } while (0)
#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)
#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)
#define flush_icache_page(vma, pg)		do { } while (0)
#define flush_icache_user_range(vma, pg, adr, len)	do { } while (0)

/* Flush the icache just on this cpu */
extern void __flush_icache_range(unsigned long start, unsigned long end);

/* Flush the entire icache on this cpu. */
#define __flush_icache() __flush_icache_range(0, CHIP_L1I_CACHE_SIZE())

#ifdef CONFIG_SMP
/*
 * When the kernel writes to its own text we need to do an SMP
 * broadcast to make the L1I coherent everywhere.  This includes
 * module load and single step.
 */
extern void flush_icache_range(unsigned long start, unsigned long end);
#else
#define flush_icache_range __flush_icache_range
#endif

/*
 * An update to an executable user page requires icache flushing.
 * We could carefully update only tiles that are running this process,
 * and rely on the fact that we flush the icache on every context
 * switch to avoid doing extra work here.  But for now, I'll be
 * conservative and just do a global icache flush.
 */
static inline void copy_to_user_page(struct vm_area_struct *vma,
				     struct page *page, unsigned long vaddr,
				     void *dst, void *src, int len)
{
	memcpy(dst, src, len);
	if (vma->vm_flags & VM_EXEC) {
		flush_icache_range((unsigned long) dst,
				   (unsigned long) dst + len);
	}
}

#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy((dst), (src), (len))

/*
 * Invalidate a VA range; pads to L2 cacheline boundaries.
 *
 * Note that on TILE64, __inv_buffer() actually flushes modified
 * cache lines in addition to invalidating them, i.e., it's the
 * same as __finv_buffer().
 */
static inline void __inv_buffer(void *buffer, size_t size)
{
	char *next = (char *)((long)buffer & -L2_CACHE_BYTES);
	char *finish = (char *)L2_CACHE_ALIGN((long)buffer + size);
	while (next < finish) {
		__insn_inv(next);
		next += CHIP_INV_STRIDE();
	}
}

/* Flush a VA range; pads to L2 cacheline boundaries. */
static inline void __flush_buffer(void *buffer, size_t size)
{
	char *next = (char *)((long)buffer & -L2_CACHE_BYTES);
	char *finish = (char *)L2_CACHE_ALIGN((long)buffer + size);
	while (next < finish) {
		__insn_flush(next);
		next += CHIP_FLUSH_STRIDE();
	}
}

/* Flush & invalidate a VA range; pads to L2 cacheline boundaries. */
static inline void __finv_buffer(void *buffer, size_t size)
{
	char *next = (char *)((long)buffer & -L2_CACHE_BYTES);
	char *finish = (char *)L2_CACHE_ALIGN((long)buffer + size);
	while (next < finish) {
		__insn_finv(next);
		next += CHIP_FINV_STRIDE();
	}
}


/* Invalidate a VA range and wait for it to be complete. */
static inline void inv_buffer(void *buffer, size_t size)
{
	__inv_buffer(buffer, size);
	mb();
}

/*
 * Flush a locally-homecached VA range and wait for the evicted
 * cachelines to hit memory.
 */
static inline void flush_buffer_local(void *buffer, size_t size)
{
	__flush_buffer(buffer, size);
	mb_incoherent();
}

/*
 * Flush and invalidate a locally-homecached VA range and wait for the
 * evicted cachelines to hit memory.
 */
static inline void finv_buffer_local(void *buffer, size_t size)
{
	__finv_buffer(buffer, size);
	mb_incoherent();
}

/*
 * Flush and invalidate a VA range that is homed remotely, waiting
 * until the memory controller holds the flushed values.  If "hfh" is
 * true, we will do a more expensive flush involving additional loads
 * to make sure we have touched all the possible home cpus of a buffer
 * that is homed with "hash for home".
 */
void finv_buffer_remote(void *buffer, size_t size, int hfh);

/*
 * On SMP systems, when the scheduler does migration-cost autodetection,
 * it needs a way to flush as much of the CPU's caches as possible:
 *
 * TODO: fill this in!
 */
static inline void sched_cacheflush(void)
{
}

#endif /* _ASM_TILE_CACHEFLUSH_H */
