/*
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2007 John Williams <john.williams@petalogix.com>
 * based on v850 version which was
 * Copyright (C) 2001,02,03 NEC Electronics Corporation
 * Copyright (C) 2001,02,03 Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 *
 */

#ifndef _ASM_MICROBLAZE_CACHEFLUSH_H
#define _ASM_MICROBLAZE_CACHEFLUSH_H

/* Somebody depends on this; sigh... */
#include <linux/mm.h>

/*
 * Cache handling functions.
 * Microblaze has a write-through data cache, meaning that the data cache
 * never needs to be flushed.  The only flushing operations that are
 * implemented are to invalidate the instruction cache.  These are called
 * after loading a user application into memory, we must invalidate the
 * instruction cache to make sure we don't fetch old, bad code.
 */

/* FIXME for LL-temac driver */
#define invalidate_dcache_range(start, end) \
			__invalidate_dcache_range(start, end)

#define flush_cache_all()			__invalidate_cache_all()
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	__invalidate_cache_all()
#define flush_cache_page(vma, vmaddr, pfn)	do { } while (0)

#define flush_dcache_range(start, end)	__invalidate_dcache_range(start, end)
#define flush_dcache_page(page)			do { } while (0)
#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

#define flush_icache_range(start, len)	__invalidate_icache_range(start, len)
#define flush_icache_page(vma, pg)		do { } while (0)

#ifndef CONFIG_MMU
# define flush_icache_user_range(start, len)	do { } while (0)
#else
# define flush_icache_user_range(vma, pg, adr, len) __invalidate_icache_all()

# define flush_page_to_ram(page)		do { } while (0)

# define flush_icache()			__invalidate_icache_all()
# define flush_cache_sigtramp(vaddr) \
			__invalidate_icache_range(vaddr, vaddr + 8)

# define flush_dcache_mmap_lock(mapping)	do { } while (0)
# define flush_dcache_mmap_unlock(mapping)	do { } while (0)

# define flush_cache_dup_mm(mm)			do { } while (0)
#endif

#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

struct page;
struct mm_struct;
struct vm_area_struct;

/* see arch/microblaze/kernel/cache.c */
extern void __invalidate_icache_all(void);
extern void __invalidate_icache_range(unsigned long start, unsigned long end);
extern void __invalidate_icache_page(struct vm_area_struct *vma,
				struct page *page);
extern void __invalidate_icache_user_range(struct vm_area_struct *vma,
				struct page *page,
				unsigned long adr, int len);
extern void __invalidate_cache_sigtramp(unsigned long addr);

extern void __invalidate_dcache_all(void);
extern void __invalidate_dcache_range(unsigned long start, unsigned long end);
extern void __invalidate_dcache_page(struct vm_area_struct *vma,
				struct page *page);
extern void __invalidate_dcache_user_range(struct vm_area_struct *vma,
				struct page *page,
				unsigned long adr, int len);

extern inline void __invalidate_cache_all(void)
{
	__invalidate_icache_all();
	__invalidate_dcache_all();
}

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
do { memcpy((dst), (src), (len)); \
	flush_icache_range((unsigned) (dst), (unsigned) (dst) + (len)); \
} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy((dst), (src), (len))

#endif /* _ASM_MICROBLAZE_CACHEFLUSH_H */
