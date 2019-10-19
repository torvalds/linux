/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#ifndef __ASM_CSKY_IO_H
#define __ASM_CSKY_IO_H

#include <asm/pgtable.h>
#include <linux/types.h>
#include <linux/version.h>

/*
 * I/O memory access primitives. Reads are ordered relative to any
 * following Normal memory access. Writes are ordered relative to any prior
 * Normal memory access.
 *
 * For CACHEV1 (807, 810), store instruction could fast retire, so we need
 * another mb() to prevent st fast retire.
 *
 * For CACHEV2 (860), store instruction with PAGE_ATTR_NO_BUFFERABLE won't
 * fast retire.
 */
#define readb(c)		({ u8  __v = readb_relaxed(c); rmb(); __v; })
#define readw(c)		({ u16 __v = readw_relaxed(c); rmb(); __v; })
#define readl(c)		({ u32 __v = readl_relaxed(c); rmb(); __v; })

#ifdef CONFIG_CPU_HAS_CACHEV2
#define writeb(v,c)		({ wmb(); writeb_relaxed((v),(c)); })
#define writew(v,c)		({ wmb(); writew_relaxed((v),(c)); })
#define writel(v,c)		({ wmb(); writel_relaxed((v),(c)); })
#else
#define writeb(v,c)		({ wmb(); writeb_relaxed((v),(c)); mb(); })
#define writew(v,c)		({ wmb(); writew_relaxed((v),(c)); mb(); })
#define writel(v,c)		({ wmb(); writel_relaxed((v),(c)); mb(); })
#endif

/*
 * I/O memory mapping functions.
 */
extern void __iomem *ioremap_cache(phys_addr_t addr, size_t size);
extern void __iomem *__ioremap(phys_addr_t addr, size_t size, pgprot_t prot);
extern void iounmap(void *addr);

#define ioremap(addr, size)		__ioremap((addr), (size), pgprot_noncached(PAGE_KERNEL))
#define ioremap_wc(addr, size)		__ioremap((addr), (size), pgprot_writecombine(PAGE_KERNEL))
#define ioremap_nocache(addr, size)	ioremap((addr), (size))
#define ioremap_cache			ioremap_cache

#include <asm-generic/io.h>

#endif /* __ASM_CSKY_IO_H */
