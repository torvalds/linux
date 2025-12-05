/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/cpu.h>
#include <asm/page.h>
#include <asm/pgtable-bits.h>
#include <asm/string.h>

extern void __init __iomem *early_ioremap(phys_addr_t phys_addr, unsigned long size);
extern void __init early_iounmap(void __iomem *addr, unsigned long size);

#define early_memremap early_ioremap
#define early_memunmap early_iounmap

#ifdef CONFIG_ARCH_IOREMAP

static inline void __iomem *ioremap_prot(phys_addr_t offset, unsigned long size,
					 pgprot_t prot)
{
	if (offset > TO_PHYS_MASK)
		return NULL;

	switch (pgprot_val(prot) & _CACHE_MASK) {
	case _CACHE_CC:
		return (void __iomem *)(unsigned long)(CACHE_BASE + offset);
	case _CACHE_SUC:
		return (void __iomem *)(unsigned long)(UNCACHE_BASE + offset);
	case _CACHE_WUC:
		return (void __iomem *)(unsigned long)(WRITECOMBINE_BASE + offset);
	default:
		return NULL;
	}
}

#define ioremap(offset, size)		\
	ioremap_prot((offset), (size), PAGE_KERNEL_SUC)

#define iounmap(addr) 			((void)(addr))

#endif

/*
 * On LoongArch, ioremap() has two variants, ioremap_wc() and ioremap_cache().
 * They map bus memory into CPU space, the mapped memory is marked uncachable
 * (_CACHE_SUC), uncachable but accelerated by write-combine (_CACHE_WUC) and
 * cachable (_CACHE_CC) respectively for CPU access.
 *
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 */
#define ioremap_wc(offset, size)	\
	ioremap_prot((offset), (size),	\
		     wc_enabled ? PAGE_KERNEL_WUC : PAGE_KERNEL_SUC)

#define ioremap_cache(offset, size)	\
	ioremap_prot((offset), (size), PAGE_KERNEL)

#define mmiowb() wmb()

#define __io_aw() mmiowb()

#ifdef CONFIG_KFENCE
#define virt_to_phys(kaddr)								\
({											\
	(likely((unsigned long)kaddr < vm_map_base)) ? __pa((unsigned long)kaddr) :	\
	page_to_phys(tlb_virt_to_page((unsigned long)kaddr)) + offset_in_page((unsigned long)kaddr);\
})

#define phys_to_virt(paddr)								\
({											\
	extern char *__kfence_pool;							\
	(unlikely(__kfence_pool == NULL)) ? __va((unsigned long)paddr) :		\
	page_address(phys_to_page((unsigned long)paddr)) + offset_in_page((unsigned long)paddr);\
})
#endif

#include <asm-generic/io.h>

#define ARCH_HAS_VALID_PHYS_ADDR_RANGE
extern int valid_phys_addr_range(phys_addr_t addr, size_t size);
extern int valid_mmap_phys_addr_range(unsigned long pfn, size_t size);

#endif /* _ASM_IO_H */
