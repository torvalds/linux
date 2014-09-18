/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_IO_H
#define _ASM_ARC_IO_H

#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/page.h>

#define PCI_IOBASE ((void __iomem *)0)

extern void __iomem *ioremap(unsigned long physaddr, unsigned long size);
extern void __iomem *ioremap_prot(phys_addr_t offset, unsigned long size,
				  unsigned long flags);
extern void iounmap(const void __iomem *addr);

#define ioremap_nocache(phy, sz)	ioremap(phy, sz)
#define ioremap_wc(phy, sz)		ioremap(phy, sz)

/* Change struct page to physical address */
#define page_to_phys(page)		(page_to_pfn(page) << PAGE_SHIFT)

#define __raw_readb __raw_readb
static inline u8 __raw_readb(const volatile void __iomem *addr)
{
	u8 b;

	__asm__ __volatile__(
	"	ldb%U1 %0, %1	\n"
	: "=r" (b)
	: "m" (*(volatile u8 __force *)addr)
	: "memory");

	return b;
}

#define __raw_readw __raw_readw
static inline u16 __raw_readw(const volatile void __iomem *addr)
{
	u16 s;

	__asm__ __volatile__(
	"	ldw%U1 %0, %1	\n"
	: "=r" (s)
	: "m" (*(volatile u16 __force *)addr)
	: "memory");

	return s;
}

#define __raw_readl __raw_readl
static inline u32 __raw_readl(const volatile void __iomem *addr)
{
	u32 w;

	__asm__ __volatile__(
	"	ld%U1 %0, %1	\n"
	: "=r" (w)
	: "m" (*(volatile u32 __force *)addr)
	: "memory");

	return w;
}

#define __raw_writeb __raw_writeb
static inline void __raw_writeb(u8 b, volatile void __iomem *addr)
{
	__asm__ __volatile__(
	"	stb%U1 %0, %1	\n"
	:
	: "r" (b), "m" (*(volatile u8 __force *)addr)
	: "memory");
}

#define __raw_writew __raw_writew
static inline void __raw_writew(u16 s, volatile void __iomem *addr)
{
	__asm__ __volatile__(
	"	stw%U1 %0, %1	\n"
	:
	: "r" (s), "m" (*(volatile u16 __force *)addr)
	: "memory");

}

#define __raw_writel __raw_writel
static inline void __raw_writel(u32 w, volatile void __iomem *addr)
{
	__asm__ __volatile__(
	"	st%U1 %0, %1	\n"
	:
	: "r" (w), "m" (*(volatile u32 __force *)addr)
	: "memory");

}

#define readb_relaxed readb
#define readw_relaxed readw
#define readl_relaxed readl

#include <asm-generic/io.h>

#endif /* _ASM_ARC_IO_H */
