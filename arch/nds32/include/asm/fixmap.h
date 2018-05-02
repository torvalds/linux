// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __ASM_NDS32_FIXMAP_H
#define __ASM_NDS32_FIXMAP_H

#ifdef CONFIG_HIGHMEM
#include <linux/threads.h>
#include <asm/kmap_types.h>
#endif

enum fixed_addresses {
	FIX_HOLE,
	FIX_KMAP_RESERVED,
	FIX_KMAP_BEGIN,
#ifdef CONFIG_HIGHMEM
	FIX_KMAP_END = FIX_KMAP_BEGIN + (KM_TYPE_NR * NR_CPUS),
#endif
	FIX_EARLYCON_MEM_BASE,
	__end_of_fixed_addresses
};
#define FIXADDR_TOP             ((unsigned long) (-(16 * PAGE_SIZE)))
#define FIXADDR_SIZE		((__end_of_fixed_addresses) << PAGE_SHIFT)
#define FIXADDR_START		(FIXADDR_TOP - FIXADDR_SIZE)
#define FIXMAP_PAGE_IO		__pgprot(PAGE_DEVICE)
void __set_fixmap(enum fixed_addresses idx, phys_addr_t phys, pgprot_t prot);

#include <asm-generic/fixmap.h>
#endif /* __ASM_NDS32_FIXMAP_H */
