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
#include <linux/io.h>

/* Look at Documentation/core-api/cachetlb.rst */

/*
 * Cache handling functions.
 * Microblaze has a write-through data cache, meaning that the data cache
 * never needs to be flushed.  The only flushing operations that are
 * implemented are to invalidate the instruction cache.  These are called
 * after loading a user application into memory, we must invalidate the
 * instruction cache to make sure we don't fetch old, bad code.
 */

/* struct cache, d=dcache, i=icache, fl = flush, iv = invalidate,
 * suffix r = range */
struct scache {
	/* icache */
	void (*ie)(void); /* enable */
	void (*id)(void); /* disable */
	void (*ifl)(void); /* flush */
	void (*iflr)(unsigned long a, unsigned long b);
	void (*iin)(void); /* invalidate */
	void (*iinr)(unsigned long a, unsigned long b);
	/* dcache */
	void (*de)(void); /* enable */
	void (*dd)(void); /* disable */
	void (*dfl)(void); /* flush */
	void (*dflr)(unsigned long a, unsigned long b);
	void (*din)(void); /* invalidate */
	void (*dinr)(unsigned long a, unsigned long b);
};

/* microblaze cache */
extern struct scache *mbc;

void microblaze_cache_init(void);

#define enable_icache()					mbc->ie();
#define disable_icache()				mbc->id();
#define flush_icache()					mbc->ifl();
#define flush_icache_range(start, end)			mbc->iflr(start, end);
#define invalidate_icache()				mbc->iin();
#define invalidate_icache_range(start, end)		mbc->iinr(start, end);

#define flush_icache_user_range(vma, pg, adr, len)	flush_icache();
#define flush_icache_page(vma, pg)			do { } while (0)

#define enable_dcache()					mbc->de();
#define disable_dcache()				mbc->dd();
/* FIXME for LL-temac driver */
#define invalidate_dcache()				mbc->din();
#define invalidate_dcache_range(start, end)		mbc->dinr(start, end);
#define flush_dcache()					mbc->dfl();
#define flush_dcache_range(start, end)			mbc->dflr(start, end);

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
/* MS: We have to implement it because of rootfs-jffs2 issue on WB */
#define flush_dcache_page(page) \
do { \
	unsigned long addr = (unsigned long) page_address(page); /* virtual */ \
	addr = (u32)virt_to_phys((void *)addr); \
	flush_dcache_range((unsigned) (addr), (unsigned) (addr) + PAGE_SIZE); \
} while (0);

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

#define flush_cache_dup_mm(mm)				do { } while (0)
#define flush_cache_vmap(start, end)			do { } while (0)
#define flush_cache_vunmap(start, end)			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)

#define flush_cache_page(vma, vmaddr, pfn) \
	flush_dcache_range(pfn << PAGE_SHIFT, (pfn << PAGE_SHIFT) + PAGE_SIZE);

/* MS: kgdb code use this macro, wrong len with FLASH */
#if 0
#define flush_cache_range(vma, start, len)	{	\
	flush_icache_range((unsigned) (start), (unsigned) (start) + (len)); \
	flush_dcache_range((unsigned) (start), (unsigned) (start) + (len)); \
}
#endif

#define flush_cache_range(vma, start, len) do { } while (0)

static inline void copy_to_user_page(struct vm_area_struct *vma,
				     struct page *page, unsigned long vaddr,
				     void *dst, void *src, int len)
{
	u32 addr = virt_to_phys(dst);
	memcpy(dst, src, len);
	if (vma->vm_flags & VM_EXEC) {
		invalidate_icache_range(addr, addr + PAGE_SIZE);
		flush_dcache_range(addr, addr + PAGE_SIZE);
	}
}

static inline void copy_from_user_page(struct vm_area_struct *vma,
				       struct page *page, unsigned long vaddr,
				       void *dst, void *src, int len)
{
	memcpy(dst, src, len);
}

#endif /* _ASM_MICROBLAZE_CACHEFLUSH_H */
