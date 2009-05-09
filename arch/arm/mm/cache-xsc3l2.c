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
#include <asm/system.h>
#include <asm/cputype.h>
#include <asm/cacheflush.h>
#include <asm/kmap_types.h>
#include <asm/fixmap.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include "mm.h"

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

#ifdef CONFIG_HIGHMEM
#define l2_map_save_flags(x)		raw_local_save_flags(x)
#define l2_map_restore_flags(x)		raw_local_irq_restore(x)
#else
#define l2_map_save_flags(x)		((x) = 0)
#define l2_map_restore_flags(x)		((void)(x))
#endif

static inline unsigned long l2_map_va(unsigned long pa, unsigned long prev_va,
				      unsigned long flags)
{
#ifdef CONFIG_HIGHMEM
	unsigned long va = prev_va & PAGE_MASK;
	unsigned long pa_offset = pa << (32 - PAGE_SHIFT);
	if (unlikely(pa_offset < (prev_va << (32 - PAGE_SHIFT)))) {
		/*
		 * Switching to a new page.  Because cache ops are
		 * using virtual addresses only, we must put a mapping
		 * in place for it.  We also enable interrupts for a
		 * short while and disable them again to protect this
		 * mapping.
		 */
		unsigned long idx;
		raw_local_irq_restore(flags);
		idx = KM_L2_CACHE + KM_TYPE_NR * smp_processor_id();
		va = __fix_to_virt(FIX_KMAP_BEGIN + idx);
		raw_local_irq_restore(flags | PSR_I_BIT);
		set_pte_ext(TOP_PTE(va), pfn_pte(pa >> PAGE_SHIFT, PAGE_KERNEL), 0);
		local_flush_tlb_kernel_page(va);
	}
	return va + (pa_offset >> (32 - PAGE_SHIFT));
#else
	return __phys_to_virt(pa);
#endif
}

static void xsc3_l2_inv_range(unsigned long start, unsigned long end)
{
	unsigned long vaddr, flags;

	if (start == 0 && end == -1ul) {
		xsc3_l2_inv_all();
		return;
	}

	vaddr = -1;  /* to force the first mapping */
	l2_map_save_flags(flags);

	/*
	 * Clean and invalidate partial first cache line.
	 */
	if (start & (CACHE_LINE_SIZE - 1)) {
		vaddr = l2_map_va(start & ~(CACHE_LINE_SIZE - 1), vaddr, flags);
		xsc3_l2_clean_mva(vaddr);
		xsc3_l2_inv_mva(vaddr);
		start = (start | (CACHE_LINE_SIZE - 1)) + 1;
	}

	/*
	 * Invalidate all full cache lines between 'start' and 'end'.
	 */
	while (start < (end & ~(CACHE_LINE_SIZE - 1))) {
		vaddr = l2_map_va(start, vaddr, flags);
		xsc3_l2_inv_mva(vaddr);
		start += CACHE_LINE_SIZE;
	}

	/*
	 * Clean and invalidate partial last cache line.
	 */
	if (start < end) {
		vaddr = l2_map_va(start, vaddr, flags);
		xsc3_l2_clean_mva(vaddr);
		xsc3_l2_inv_mva(vaddr);
	}

	l2_map_restore_flags(flags);

	dsb();
}

static void xsc3_l2_clean_range(unsigned long start, unsigned long end)
{
	unsigned long vaddr, flags;

	vaddr = -1;  /* to force the first mapping */
	l2_map_save_flags(flags);

	start &= ~(CACHE_LINE_SIZE - 1);
	while (start < end) {
		vaddr = l2_map_va(start, vaddr, flags);
		xsc3_l2_clean_mva(vaddr);
		start += CACHE_LINE_SIZE;
	}

	l2_map_restore_flags(flags);

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
	unsigned long vaddr, flags;

	if (start == 0 && end == -1ul) {
		xsc3_l2_flush_all();
		return;
	}

	vaddr = -1;  /* to force the first mapping */
	l2_map_save_flags(flags);

	start &= ~(CACHE_LINE_SIZE - 1);
	while (start < end) {
		vaddr = l2_map_va(start, vaddr, flags);
		xsc3_l2_clean_mva(vaddr);
		xsc3_l2_inv_mva(vaddr);
		start += CACHE_LINE_SIZE;
	}

	l2_map_restore_flags(flags);

	dsb();
}

static int __init xsc3_l2_init(void)
{
	if (!cpu_is_xsc3() || !xsc3_l2_present())
		return 0;

	if (!(get_cr() & CR_L2)) {
		pr_info("XScale3 L2 cache enabled.\n");
		adjust_cr(CR_L2, CR_L2);
		xsc3_l2_inv_all();
	}

	outer_cache.inv_range = xsc3_l2_inv_range;
	outer_cache.clean_range = xsc3_l2_clean_range;
	outer_cache.flush_range = xsc3_l2_flush_range;

	return 0;
}
core_initcall(xsc3_l2_init);
