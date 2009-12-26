/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_CACHEFLUSH_H
#define __ASM_AVR32_CACHEFLUSH_H

/* Keep includes the same across arches.  */
#include <linux/mm.h>

#define CACHE_OP_ICACHE_INVALIDATE	0x01
#define CACHE_OP_DCACHE_INVALIDATE	0x0b
#define CACHE_OP_DCACHE_CLEAN		0x0c
#define CACHE_OP_DCACHE_CLEAN_INVAL	0x0d

/*
 * Invalidate any cacheline containing virtual address vaddr without
 * writing anything back to memory.
 *
 * Note that this function may corrupt unrelated data structures when
 * applied on buffers that are not cacheline aligned in both ends.
 */
static inline void invalidate_dcache_line(void *vaddr)
{
	asm volatile("cache %0[0], %1"
		     :
		     : "r"(vaddr), "n"(CACHE_OP_DCACHE_INVALIDATE)
		     : "memory");
}

/*
 * Make sure any cacheline containing virtual address vaddr is written
 * to memory.
 */
static inline void clean_dcache_line(void *vaddr)
{
	asm volatile("cache %0[0], %1"
		     :
		     : "r"(vaddr), "n"(CACHE_OP_DCACHE_CLEAN)
		     : "memory");
}

/*
 * Make sure any cacheline containing virtual address vaddr is written
 * to memory and then invalidate it.
 */
static inline void flush_dcache_line(void *vaddr)
{
	asm volatile("cache %0[0], %1"
		     :
		     : "r"(vaddr), "n"(CACHE_OP_DCACHE_CLEAN_INVAL)
		     : "memory");
}

/*
 * Invalidate any instruction cacheline containing virtual address
 * vaddr.
 */
static inline void invalidate_icache_line(void *vaddr)
{
	asm volatile("cache %0[0], %1"
		     :
		     : "r"(vaddr), "n"(CACHE_OP_ICACHE_INVALIDATE)
		     : "memory");
}

/*
 * Applies the above functions on all lines that are touched by the
 * specified virtual address range.
 */
void invalidate_dcache_region(void *start, size_t len);
void clean_dcache_region(void *start, size_t len);
void flush_dcache_region(void *start, size_t len);
void invalidate_icache_region(void *start, size_t len);

/*
 * Make sure any pending writes are completed before continuing.
 */
#define flush_write_buffer() asm volatile("sync 0" : : : "memory")

/*
 * The following functions are called when a virtual mapping changes.
 * We do not need to flush anything in this case.
 */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_dup_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr, pfn)	do { } while (0)
#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

/*
 * I think we need to implement this one to be able to reliably
 * execute pages from RAMDISK. However, if we implement the
 * flush_dcache_*() functions, it might not be needed anymore.
 *
 * #define flush_icache_page(vma, page)		do { } while (0)
 */
extern void flush_icache_page(struct vm_area_struct *vma, struct page *page);

/*
 * These are (I think) related to D-cache aliasing.  We might need to
 * do something here, but only for certain configurations.  No such
 * configurations exist at this time.
 */
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 0
#define flush_dcache_page(page)			do { } while (0)
#define flush_dcache_mmap_lock(page)		do { } while (0)
#define flush_dcache_mmap_unlock(page)		do { } while (0)

/*
 * These are for I/D cache coherency. In this case, we do need to
 * flush with all configurations.
 */
extern void flush_icache_range(unsigned long start, unsigned long end);

extern void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
		unsigned long vaddr, void *dst, const void *src,
		unsigned long len);

static inline void copy_from_user_page(struct vm_area_struct *vma,
		struct page *page, unsigned long vaddr, void *dst,
		const void *src, unsigned long len)
{
	memcpy(dst, src, len);
}

#endif /* __ASM_AVR32_CACHEFLUSH_H */
