/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

#define FIXADDR_START		0xffc00000UL
#define FIXADDR_END		0xfff00000UL
#define FIXADDR_TOP		(FIXADDR_END - PAGE_SIZE)

#include <linux/pgtable.h>
#include <asm/kmap_types.h>

enum fixed_addresses {
	FIX_EARLYCON_MEM_BASE,
	__end_of_permanent_fixed_addresses,

	FIX_KMAP_BEGIN = __end_of_permanent_fixed_addresses,
	FIX_KMAP_END = FIX_KMAP_BEGIN + (KM_TYPE_NR * NR_CPUS) - 1,

	/* Support writing RO kernel text via kprobes, jump labels, etc. */
	FIX_TEXT_POKE0,
	FIX_TEXT_POKE1,

	__end_of_fixmap_region,

	/*
	 * Share the kmap() region with early_ioremap(): this is guaranteed
	 * not to clash since early_ioremap() is only available before
	 * paging_init(), and kmap() only after.
	 */
#define NR_FIX_BTMAPS		32
#define FIX_BTMAPS_SLOTS	7
#define TOTAL_FIX_BTMAPS	(NR_FIX_BTMAPS * FIX_BTMAPS_SLOTS)

	FIX_BTMAP_END = __end_of_permanent_fixed_addresses,
	FIX_BTMAP_BEGIN = FIX_BTMAP_END + TOTAL_FIX_BTMAPS - 1,
	__end_of_early_ioremap_region
};

static const enum fixed_addresses __end_of_fixed_addresses =
	__end_of_fixmap_region > __end_of_early_ioremap_region ?
	__end_of_fixmap_region : __end_of_early_ioremap_region;

#define FIXMAP_PAGE_COMMON	(L_PTE_YOUNG | L_PTE_PRESENT | L_PTE_XN | L_PTE_DIRTY)

#define FIXMAP_PAGE_NORMAL	(pgprot_kernel | L_PTE_XN)
#define FIXMAP_PAGE_RO		(FIXMAP_PAGE_NORMAL | L_PTE_RDONLY)

/* Used by set_fixmap_(io|nocache), both meant for mapping a device */
#define FIXMAP_PAGE_IO		(FIXMAP_PAGE_COMMON | L_PTE_MT_DEV_SHARED | L_PTE_SHARED)
#define FIXMAP_PAGE_NOCACHE	FIXMAP_PAGE_IO

#define __early_set_fixmap	__set_fixmap

#ifdef CONFIG_MMU

void __set_fixmap(enum fixed_addresses idx, phys_addr_t phys, pgprot_t prot);
void __init early_fixmap_init(void);

#include <asm-generic/fixmap.h>

#else

static inline void early_fixmap_init(void) { }

#endif
#endif
