/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_IO_H
#define _ASM_IO_H

#define ARCH_HAS_IOREMAP_WC

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/bug.h>
#include <asm/byteorder.h>
#include <asm/cpu.h>
#include <asm/page.h>
#include <asm/pgtable-bits.h>
#include <asm/string.h>

/*
 * On LoongArch, I/O ports mappring is following:
 *
 *              |         ....          |
 *              |-----------------------|
 *              | pci io ports(64K~32M) |
 *              |-----------------------|
 *              | isa io ports(0  ~16K) |
 * PCI_IOBASE ->|-----------------------|
 *              |         ....          |
 */
#define PCI_IOBASE	((void __iomem *)(vm_map_base + (2 * PAGE_SIZE)))
#define PCI_IOSIZE	SZ_32M
#define ISA_IOSIZE	SZ_16K
#define IO_SPACE_LIMIT	(PCI_IOSIZE - 1)

/*
 * Change "struct page" to physical address.
 */
#define page_to_phys(page)	((phys_addr_t)page_to_pfn(page) << PAGE_SHIFT)

extern void __init __iomem *early_ioremap(u64 phys_addr, unsigned long size);
extern void __init early_iounmap(void __iomem *addr, unsigned long size);

#define early_memremap early_ioremap
#define early_memunmap early_iounmap

static inline void __iomem *ioremap_prot(phys_addr_t offset, unsigned long size,
					 unsigned long prot_val)
{
	if (prot_val == _CACHE_CC)
		return (void __iomem *)(unsigned long)(CACHE_BASE + offset);
	else
		return (void __iomem *)(unsigned long)(UNCACHE_BASE + offset);
}

/*
 * ioremap -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 */
#define ioremap(offset, size)					\
	ioremap_prot((offset), (size), _CACHE_SUC)

/*
 * ioremap_wc - map bus memory into CPU space
 * @offset:     bus address of the memory
 * @size:       size of the resource to map
 *
 * ioremap_wc performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * This version of ioremap ensures that the memory is marked uncachable
 * but accelerated by means of write-combining feature. It is specifically
 * useful for PCIe prefetchable windows, which may vastly improve a
 * communications performance. If it was determined on boot stage, what
 * CPU CCA doesn't support WUC, the method shall fall-back to the
 * _CACHE_SUC option (see cpu_probe() method).
 */
#define ioremap_wc(offset, size)				\
	ioremap_prot((offset), (size), _CACHE_WUC)

/*
 * ioremap_cache -  map bus memory into CPU space
 * @offset:	    bus address of the memory
 * @size:	    size of the resource to map
 *
 * ioremap_cache performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * This version of ioremap ensures that the memory is marked cachable by
 * the CPU.  Also enables full write-combining.	 Useful for some
 * memory-like regions on I/O busses.
 */
#define ioremap_cache(offset, size)				\
	ioremap_prot((offset), (size), _CACHE_CC)

static inline void iounmap(const volatile void __iomem *addr)
{
}

#define mmiowb() asm volatile ("dbar 0" ::: "memory")

/*
 * String version of I/O memory access operations.
 */
extern void __memset_io(volatile void __iomem *dst, int c, size_t count);
extern void __memcpy_toio(volatile void __iomem *to, const void *from, size_t count);
extern void __memcpy_fromio(void *to, const volatile void __iomem *from, size_t count);
#define memset_io(c, v, l)     __memset_io((c), (v), (l))
#define memcpy_fromio(a, c, l) __memcpy_fromio((a), (c), (l))
#define memcpy_toio(c, a, l)   __memcpy_toio((c), (a), (l))

#include <asm-generic/io.h>

#endif /* _ASM_IO_H */
