/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_FIXMAP_H
#define __ASM_CSKY_FIXMAP_H

#include <asm/page.h>
#include <asm/memory.h>
#ifdef CONFIG_HIGHMEM
#include <linux/threads.h>
#include <asm/kmap_types.h>
#endif

enum fixed_addresses {
#ifdef CONFIG_HAVE_TCM
	FIX_TCM = TCM_NR_PAGES,
#endif
#ifdef CONFIG_HIGHMEM
	FIX_KMAP_BEGIN,
	FIX_KMAP_END = FIX_KMAP_BEGIN + (KM_TYPE_NR * NR_CPUS) - 1,
#endif
	__end_of_fixed_addresses
};

#define FIXADDR_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START	(FIXADDR_TOP - FIXADDR_SIZE)

#include <asm-generic/fixmap.h>

extern void fixrange_init(unsigned long start, unsigned long end,
	pgd_t *pgd_base);
extern void __init fixaddr_init(void);

#endif /* __ASM_CSKY_FIXMAP_H */
