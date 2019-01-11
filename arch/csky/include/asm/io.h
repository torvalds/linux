/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_IO_H
#define __ASM_CSKY_IO_H

#include <abi/pgtable-bits.h>
#include <linux/types.h>
#include <linux/version.h>

extern void __iomem *ioremap(phys_addr_t offset, size_t size);

extern void iounmap(void *addr);

extern int remap_area_pages(unsigned long address, phys_addr_t phys_addr,
		size_t size, unsigned long flags);

#define ioremap_nocache(phy, sz)	ioremap(phy, sz)
#define ioremap_wc ioremap_nocache
#define ioremap_wt ioremap_nocache

#include <asm-generic/io.h>

#endif /* __ASM_CSKY_IO_H */
