#ifndef _M68KNOMMU_CACHEFLUSH_H
#define _M68KNOMMU_CACHEFLUSH_H

/*
 * (C) Copyright 2000-2004, Greg Ungerer <gerg@snapgear.com>
 */
#include <linux/mm.h>
#include <asm/mcfsim.h>

#define flush_cache_all()			__flush_cache_all()
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_dup_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	__flush_cache_all()
#define flush_cache_page(vma, vmaddr)		do { } while (0)
#ifndef flush_dcache_range
#define flush_dcache_range(start,len)		__flush_cache_all()
#endif
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 0
#define flush_dcache_page(page)			do { } while (0)
#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)
#define flush_icache_range(start,len)		__flush_cache_all()
#define flush_icache_page(vma,pg)		do { } while (0)
#define flush_icache_user_range(vma,pg,adr,len)	do { } while (0)
#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

#ifndef __flush_cache_all
static inline void __flush_cache_all(void)
{
#if defined(CONFIG_M523x) || defined(CONFIG_M527x)
	__asm__ __volatile__ (
		"movel	#0x81400110, %%d0\n\t"
		"movec	%%d0, %%CACR\n\t"
		"nop\n\t"
		: : : "d0" );
#endif /* CONFIG_M523x || CONFIG_M527x */
#if defined(CONFIG_M528x)
	__asm__ __volatile__ (
		"movel	#0x81000200, %%d0\n\t"
		"movec	%%d0, %%CACR\n\t"
		"nop\n\t"
		: : : "d0" );
#endif /* CONFIG_M528x */
#if defined(CONFIG_M5206) || defined(CONFIG_M5206e) || defined(CONFIG_M5272)
	__asm__ __volatile__ (
		"movel	#0x81000100, %%d0\n\t"
		"movec	%%d0, %%CACR\n\t"
		"nop\n\t"
		: : : "d0" );
#endif /* CONFIG_M5206 || CONFIG_M5206e || CONFIG_M5272 */
#ifdef CONFIG_M5249
	__asm__ __volatile__ (
		"movel	#0xa1000200, %%d0\n\t"
		"movec	%%d0, %%CACR\n\t"
		"nop\n\t"
		: : : "d0" );
#endif /* CONFIG_M5249 */
#ifdef CONFIG_M532x
	__asm__ __volatile__ (
		"movel	#0x81000210, %%d0\n\t"
		"movec	%%d0, %%CACR\n\t"
		"nop\n\t"
		: : : "d0" );
#endif /* CONFIG_M532x */
}
#endif /* __flush_cache_all */

#endif /* _M68KNOMMU_CACHEFLUSH_H */
