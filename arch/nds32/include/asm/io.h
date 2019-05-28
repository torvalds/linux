/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __ASM_NDS32_IO_H
#define __ASM_NDS32_IO_H

#include <linux/types.h>

extern void iounmap(volatile void __iomem *addr);
#define __raw_writeb __raw_writeb
static inline void __raw_writeb(u8 val, volatile void __iomem *addr)
{
	asm volatile("sbi %0, [%1]" : : "r" (val), "r" (addr));
}

#define __raw_writew __raw_writew
static inline void __raw_writew(u16 val, volatile void __iomem *addr)
{
	asm volatile("shi %0, [%1]" : : "r" (val), "r" (addr));
}

#define __raw_writel __raw_writel
static inline void __raw_writel(u32 val, volatile void __iomem *addr)
{
	asm volatile("swi %0, [%1]" : : "r" (val), "r" (addr));
}

#define __raw_readb __raw_readb
static inline u8 __raw_readb(const volatile void __iomem *addr)
{
	u8 val;

	asm volatile("lbi %0, [%1]" : "=r" (val) : "r" (addr));
	return val;
}

#define __raw_readw __raw_readw
static inline u16 __raw_readw(const volatile void __iomem *addr)
{
	u16 val;

	asm volatile("lhi %0, [%1]" : "=r" (val) : "r" (addr));
	return val;
}

#define __raw_readl __raw_readl
static inline u32 __raw_readl(const volatile void __iomem *addr)
{
	u32 val;

	asm volatile("lwi %0, [%1]" : "=r" (val) : "r" (addr));
	return val;
}

#define __iormb()               rmb()
#define __iowmb()               wmb()

/*
 * {read,write}{b,w,l,q}_relaxed() are like the regular version, but
 * are not guaranteed to provide ordering against spinlocks or memory
 * accesses.
 */

#define readb_relaxed(c)	({ u8  __v = __raw_readb(c); __v; })
#define readw_relaxed(c)	({ u16 __v = le16_to_cpu((__force __le16)__raw_readw(c)); __v; })
#define readl_relaxed(c)	({ u32 __v = le32_to_cpu((__force __le32)__raw_readl(c)); __v; })
#define writeb_relaxed(v,c)	((void)__raw_writeb((v),(c)))
#define writew_relaxed(v,c)	((void)__raw_writew((__force u16)cpu_to_le16(v),(c)))
#define writel_relaxed(v,c)	((void)__raw_writel((__force u32)cpu_to_le32(v),(c)))

/*
 * {read,write}{b,w,l,q}() access little endian memory and return result in
 * native endianness.
 */
#define readb(c)	({ u8  __v = readb_relaxed(c); __iormb(); __v; })
#define readw(c)	({ u16 __v = readw_relaxed(c); __iormb(); __v; })
#define readl(c)	({ u32 __v = readl_relaxed(c); __iormb(); __v; })

#define writeb(v,c)	({ __iowmb(); writeb_relaxed((v),(c)); })
#define writew(v,c)	({ __iowmb(); writew_relaxed((v),(c)); })
#define writel(v,c)	({ __iowmb(); writel_relaxed((v),(c)); })
#include <asm-generic/io.h>
#endif /* __ASM_NDS32_IO_H */
