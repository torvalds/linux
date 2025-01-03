/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IO definitions for the Hexagon architecture
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_IO_H
#define _ASM_IO_H

#include <linux/types.h>
#include <asm/page.h>
#include <asm/cacheflush.h>

extern int remap_area_pages(unsigned long start, unsigned long phys_addr,
				unsigned long end, unsigned long flags);

/*
 * virt_to_phys - map virtual address to physical
 * @address:  address to map
 */
static inline unsigned long virt_to_phys(volatile void *address)
{
	return __pa(address);
}

/*
 * phys_to_virt - map physical address to virtual
 * @address: address to map
 */
static inline void *phys_to_virt(unsigned long address)
{
	return __va(address);
}

/*
 * readb - read byte from memory mapped device
 * @addr:  pointer to memory
 *
 */
static inline u8 __raw_readb(const volatile void __iomem *addr)
{
	u8 val;
	asm volatile(
		"%0 = memb(%1);"
		: "=&r" (val)
		: "r" (addr)
	);
	return val;
}
#define __raw_readb __raw_readb

static inline u16 __raw_readw(const volatile void __iomem *addr)
{
	u16 val;
	asm volatile(
		"%0 = memh(%1);"
		: "=&r" (val)
		: "r" (addr)
	);
	return val;
}
#define __raw_readw __raw_readw

static inline u32 __raw_readl(const volatile void __iomem *addr)
{
	u32 val;
	asm volatile(
		"%0 = memw(%1);"
		: "=&r" (val)
		: "r" (addr)
	);
	return val;
}
#define __raw_readl __raw_readl

/*
 * writeb - write a byte to a memory location
 * @data: data to write to
 * @addr:  pointer to memory
 *
 */
static inline void __raw_writeb(u8 data, volatile void __iomem *addr)
{
	asm volatile(
		"memb(%0) = %1;"
		:
		: "r" (addr), "r" (data)
		: "memory"
	);
}
#define __raw_writeb __raw_writeb

static inline void __raw_writew(u16 data, volatile void __iomem *addr)
{
	asm volatile(
		"memh(%0) = %1;"
		:
		: "r" (addr), "r" (data)
		: "memory"
	);

}
#define __raw_writew __raw_writew

static inline void __raw_writel(u32 data, volatile void __iomem *addr)
{
	asm volatile(
		"memw(%0) = %1;"
		:
		: "r" (addr), "r" (data)
		: "memory"
	);
}
#define __raw_writel __raw_writel

/*
 * I/O memory mapping functions.
 */
#define _PAGE_IOREMAP (_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
		       (__HEXAGON_C_DEV << 6))

/*
 * These defines are necessary to use the generic io.h for filling in
 * the missing parts of the API contract. This is because the platform
 * uses (inline) functions rather than defines and the generic helper
 * fills in the undefined.
 */
#define virt_to_phys virt_to_phys
#define phys_to_virt phys_to_virt
#include <asm-generic/io.h>

#endif
