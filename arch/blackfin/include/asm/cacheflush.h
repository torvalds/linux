/*
 * File:         include/asm-blackfin/cacheflush.h
 * Based on:	 include/asm-m68knommu/cacheflush.h
 * Author:       LG Soft India
 *               Copyright (C) 2004 Analog Devices Inc.
 * Created:      Tue Sep 21 2004
 * Description:  Blackfin low-level cache routines adapted from the i386
 * 		 and PPC versions by Greg Ungerer (gerg@snapgear.com)
 *
 * Modified:
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _BLACKFIN_CACHEFLUSH_H
#define _BLACKFIN_CACHEFLUSH_H

extern void blackfin_icache_dcache_flush_range(unsigned long start_address, unsigned long end_address);
extern void blackfin_icache_flush_range(unsigned long start_address, unsigned long end_address);
extern void blackfin_dcache_flush_range(unsigned long start_address, unsigned long end_address);
extern void blackfin_dcache_invalidate_range(unsigned long start_address, unsigned long end_address);
extern void blackfin_dflush_page(void *page);
extern void blackfin_invalidate_entire_dcache(void);

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr)		do { } while (0)
#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

#ifdef CONFIG_SMP
#define flush_icache_range_others(start, end)	\
	smp_icache_flush_range_others((start), (end))
#else
#define flush_icache_range_others(start, end)	do { } while (0)
#endif

static inline void flush_icache_range(unsigned start, unsigned end)
{
#if defined(CONFIG_BFIN_DCACHE) && defined(CONFIG_BFIN_ICACHE)

# if defined(CONFIG_BFIN_WT)
	blackfin_icache_flush_range((start), (end));
	flush_icache_range_others(start, end);
# else
	blackfin_icache_dcache_flush_range((start), (end));
# endif

#else

# if defined(CONFIG_BFIN_ICACHE)
	blackfin_icache_flush_range((start), (end));
	flush_icache_range_others(start, end);
# endif
# if defined(CONFIG_BFIN_DCACHE)
	blackfin_dcache_flush_range((start), (end));
# endif

#endif
}

#define copy_to_user_page(vma, page, vaddr, dst, src, len)		\
do { memcpy(dst, src, len);						\
     flush_icache_range((unsigned) (dst), (unsigned) (dst) + (len));	\
     flush_icache_range_others((unsigned long) (dst), (unsigned long) (dst) + (len));\
} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len)	memcpy(dst, src, len)

#if defined(CONFIG_BFIN_DCACHE)
# define invalidate_dcache_range(start,end)	blackfin_dcache_invalidate_range((start), (end))
#else
# define invalidate_dcache_range(start,end)	do { } while (0)
#endif
#if defined(CONFIG_BFIN_DCACHE) && defined(CONFIG_BFIN_WB)
# define flush_dcache_range(start,end)		blackfin_dcache_flush_range((start), (end))
# define flush_dcache_page(page)			blackfin_dflush_page(page_address(page))
#else
# define flush_dcache_range(start,end)		do { } while (0)
# define flush_dcache_page(page)		do { } while (0)
#endif

extern unsigned long reserved_mem_dcache_on;
extern unsigned long reserved_mem_icache_on;

static inline int bfin_addr_dcachable(unsigned long addr)
{
#ifdef CONFIG_BFIN_DCACHE
	if (addr < (_ramend - DMA_UNCACHED_REGION))
		return 1;
#endif

	if (reserved_mem_dcache_on &&
		addr >= _ramend && addr < physical_mem_end)
		return 1;

	return 0;
}

#endif				/* _BLACKFIN_ICACHEFLUSH_H */
