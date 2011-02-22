/*
 * arch/arm/mm/cache-xsc3l2.c - XScale3 L2 cache controller support
 *
 * Copyright (C) 2007 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include <linux/init.h>
#include <linux/highmem.h>
#include <asm/system.h>
#include <asm/cputype.h>
#include <asm/cacheflush.h>

#define CR_L2	(1 << 26)

#define CACHE_LINE_SIZE		32
#define CACHE_LINE_SHIFT	5
#define CACHE_WAY_PER_SET	8

#define CACHE_WAY_SIZE(l2ctype)	(8192 << (((l2ctype) >> 8) & 0xf))
#define CACHE_SET_SIZE(l2ctype)	(CACHE_WAY_SIZE(l2ctype) >> CACHE_LINE_SHIFT)

static inline int xsc3_l2_present(void)
{
	unsigned long l2ctype;

	__asm__("mrc p15, 1, %0, c0, c0, 1" : "=r" (l2ctype));

	return !!(l2ctype & 0xf8);
}

static inline void xsc3_l2_clean_mva(unsigned long addr)
{
	__asm__("mcr p15, 1, %0, c7, c11, 1" : : "r" (addr));
}

static inline void xsc3_l2_inv_mva(unsigned long addr)
{
	__asm__("mcr p15, 1, %0, c7, c7, 1" : : "r" (addr));
}

static inline void xsc3_l2_inv_all(void)
{
	unsigned long l2ctype, set_way;
	int set, way;

	__asm__("mrc p15, 1, %0, c0, c0, 1" : "=r" (l2ctype));

	for (set = 0; set < CACHE_SET_SIZE(l2ctype); set++) {
		for (way = 0; way < CACHE_WAY_PER_SET; way++) {
			set_way = (way << 29) | (set << 5);
			__asm__("mcr p15, 1, %0, c7, c11, 2" : : "r"(set_way));
		}
	}

	dsb();
}

static inline void l2_unmap_va(unsigned long va)
{
#ifdef CONFIG_HIGHMEM
	if (va != -1)
		kunmap_atomic((void *)va);
#endif
}

static inline unsigned long l2_map_va(unsigned long pa, unsigned long prev_va)
{
#ifdef CONFIG_HIGHMEM
	unsigned long va = prev_va & PAGE_MASK;
	unsigned long pa_offset = pa << (32 - PAGE_SHIFT);
	if (unlikely(pa_offset < (prev_va << (32 - PAGE_SHIFT)))) {
		/*
		 * Switching to a new page.  Because cache ops are
		 * using virtual addresses only, we must put a mapping
		 * in place for it.
		 */
		l2_unmap_va(prev_va);
		va = (unsigned long)kmap_atomic_pfn(pa >> PAGE_SHIFT);
	}
	return va + (pa_offset >> (32 - PAGE_SHIFT));
#else
	return __phys_to_virt(pa);
#endif
}

static void xsc3_l2_inv_range(unsigned long start, unsigned long end)
{
	unsigned long vaddr;

	if (start == 0 && end == -1ul) {
		xsc3_l2_inv_all();
		return;
	}

	vaddr = -1;  /* to force the first mapping */

	/*
	 * Clean and invalidate partial first cache line.
	 */
	if (start & (CACHE_LINE_SIZE - 1)) {
		vaddr = l2_map_va(start & ~(CACHE_LINE_SIZE - 1), vaddr);
		xsc3_l2_clean_mva(vaddr);
		xsc3_l2_inv_mva(vaddr);
		start = (start | (CACHE_LINE_SIZE - 1)) + 1;
	}

	/*
	 * Invalidate all full cache lines between 'start' and 'end'.
	 */
	while (start < (end & ~(CACHE_LINE_SIZE - 1))) {
		vaddr = l2_map_va(start, vaddr);
		xsc3_l2_inv_mva(vaddr);
		start += CACHE_LINE_SIZE;
	}

	/*
	 * Clean and invalidate partial last cache line.
	 */
	if (start < end) {
		vaddr = l2_map_va(start, vaddr);
		xsc3_l2_clean_mva(vaddr);
		xsc3_l2_inv_mva(vaddr);
	}

	l2_unmap_va(vaddr);

	dsb();
}

static void xsc3_l2_clean_range(unsigned long start, unsigned long end)
{
	unsigned long vaddr;

	vaddr = -1;  /* to force the first mapping */

	start &= ~(CACHE_LINE_SIZE - 1);
	while (start < end) {
		vaddr = l2_map_va(start, vaddr);
		xsc3_l2_clean_mva(vaddr);
		start += CACHE_LINE_SIZE;
	}

	l2_unmap_va(vaddr);

	dsb();
}

/*
 * optimize L2 flush all operation by set/way format
 */
static inline void xsc3_l2_flush_all(void)
{
	unsigned long l2ctype, set_way;
	int set, way;

	__asm__("mrc p15, 1, %0, c0, c0, 1" : "=r" (l2ctype));

	for (set = 0; set < CACHE_SET_SIZE(l2ctype); set++) {
		for (way = 0; way < CACHE_WAY_PER_SET; way++) {
			set_way = (way << 29) | (set << 5);
			__asm__("mcr p15, 1, %0, c7, c15, 2" : : "r"(set_way));
		}
	}

	dsb();
}

static void xsc3_l2_flush_range(unsigned long start, unsigned long end)
{
	unsigned long vaddr;

	if (start == 0 && end == -1ul) {
		xsc3_l2_flush_all();
		return;
	}

	vaddr = -1;  /* to force the first mapping */

	start &= ~(CACHE_LINE_SIZE - 1);
	while (start < end) {
		vaddr = l2_map_va(start, vaddr);
		xsc3_l2_clean_mva(vaddr);
		xsc3_l2_inv_mva(vaddr);
		start += CACHE_LINE_SIZE;
	}

	l2_unmap_va(vaddr);

	dsb();
}

static int __init xsc3_l2_init(void)
{
	if (!cpu_is_xsc3() || !xsc3_l2_present())
		return 0;

	if (get_cr() & CR_L2) {
		pr_info("XScale3 L2 cache enabled.\n");
		xsc3_l2_inv_all();

		outer_cache.inv_range = xsc3_l2_inv_range;
		outer_cache.clean_range = xsc3_l2_clean_range;
		outer_cache.flush_range = xsc3_l2_flush_range;
	}

	return 0;
}
core_initcall(xsc3_l2_init);
