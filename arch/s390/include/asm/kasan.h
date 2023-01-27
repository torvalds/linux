/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_KASAN_H
#define __ASM_KASAN_H

#include <asm/pgtable.h>

#ifdef CONFIG_KASAN

#define KASAN_SHADOW_SCALE_SHIFT 3
#define KASAN_SHADOW_SIZE						       \
	(_AC(1, UL) << (_REGION1_SHIFT - KASAN_SHADOW_SCALE_SHIFT))
#define KASAN_SHADOW_OFFSET	_AC(CONFIG_KASAN_SHADOW_OFFSET, UL)
#define KASAN_SHADOW_START	KASAN_SHADOW_OFFSET
#define KASAN_SHADOW_END	(KASAN_SHADOW_START + KASAN_SHADOW_SIZE)

extern void kasan_early_init(void);

/*
 * Estimate kasan memory requirements, which it will reserve
 * at the very end of available physical memory. To estimate
 * that, we take into account that kasan would require
 * 1/8 of available physical memory (for shadow memory) +
 * creating page tables for the shadow memory region.
 * To keep page tables estimates simple take the double of
 * combined ptes size.
 *
 * physmem parameter has to be already adjusted if not entire physical memory
 * would be used (e.g. due to effect of "mem=" option).
 */
static inline unsigned long kasan_estimate_memory_needs(unsigned long physmem)
{
	unsigned long kasan_needs;
	unsigned long pages;
	/* for shadow memory */
	kasan_needs = round_up(physmem / 8, PAGE_SIZE);
	/* for paging structures */
	pages = DIV_ROUND_UP(kasan_needs, PAGE_SIZE);
	kasan_needs += DIV_ROUND_UP(pages, _PAGE_ENTRIES) * _PAGE_TABLE_SIZE * 2;

	return kasan_needs;
}
#else
static inline void kasan_early_init(void) { }
static inline unsigned long kasan_estimate_memory_needs(unsigned long physmem) { return 0; }
#endif

#endif
