// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/kernel/iomap.c
 *
 * Copyright (C) 2000  Niibe Yutaka
 * Copyright (C) 2005 - 2007 Paul Mundt
 */
#include <linux/module.h>
#include <linux/io.h>

unsigned int ioread8(void __iomem *addr)
{
	return readb(addr);
}
EXPORT_SYMBOL(ioread8);

unsigned int ioread16(void __iomem *addr)
{
	return readw(addr);
}
EXPORT_SYMBOL(ioread16);

unsigned int ioread16be(void __iomem *addr)
{
	return be16_to_cpu(__raw_readw(addr));
}
EXPORT_SYMBOL(ioread16be);

unsigned int ioread32(void __iomem *addr)
{
	return readl(addr);
}
EXPORT_SYMBOL(ioread32);

unsigned int ioread32be(void __iomem *addr)
{
	return be32_to_cpu(__raw_readl(addr));
}
EXPORT_SYMBOL(ioread32be);

void iowrite8(u8 val, void __iomem *addr)
{
	writeb(val, addr);
}
EXPORT_SYMBOL(iowrite8);

void iowrite16(u16 val, void __iomem *addr)
{
	writew(val, addr);
}
EXPORT_SYMBOL(iowrite16);

void iowrite16be(u16 val, void __iomem *addr)
{
	__raw_writew(cpu_to_be16(val), addr);
}
EXPORT_SYMBOL(iowrite16be);

void iowrite32(u32 val, void __iomem *addr)
{
	writel(val, addr);
}
EXPORT_SYMBOL(iowrite32);

void iowrite32be(u32 val, void __iomem *addr)
{
	__raw_writel(cpu_to_be32(val), addr);
}
EXPORT_SYMBOL(iowrite32be);

/*
 * These are the "repeat MMIO read/write" functions.
 * Note the "__raw" accesses, since we don't want to
 * convert to CPU byte order. We write in "IO byte
 * order" (we also don't have IO barriers).
 */
static inline void mmio_insb(void __iomem *addr, u8 *dst, int count)
{
	while (--count >= 0) {
		u8 data = __raw_readb(addr);
		*dst = data;
		dst++;
	}
}

static inline void mmio_insw(void __iomem *addr, u16 *dst, int count)
{
	while (--count >= 0) {
		u16 data = __raw_readw(addr);
		*dst = data;
		dst++;
	}
}

static inline void mmio_insl(void __iomem *addr, u32 *dst, int count)
{
	while (--count >= 0) {
		u32 data = __raw_readl(addr);
		*dst = data;
		dst++;
	}
}

static inline void mmio_outsb(void __iomem *addr, const u8 *src, int count)
{
	while (--count >= 0) {
		__raw_writeb(*src, addr);
		src++;
	}
}

static inline void mmio_outsw(void __iomem *addr, const u16 *src, int count)
{
	while (--count >= 0) {
		__raw_writew(*src, addr);
		src++;
	}
}

static inline void mmio_outsl(void __iomem *addr, const u32 *src, int count)
{
	while (--count >= 0) {
		__raw_writel(*src, addr);
		src++;
	}
}

void ioread8_rep(void __iomem *addr, void *dst, unsigned long count)
{
	mmio_insb(addr, dst, count);
}
EXPORT_SYMBOL(ioread8_rep);

void ioread16_rep(void __iomem *addr, void *dst, unsigned long count)
{
	mmio_insw(addr, dst, count);
}
EXPORT_SYMBOL(ioread16_rep);

void ioread32_rep(void __iomem *addr, void *dst, unsigned long count)
{
	mmio_insl(addr, dst, count);
}
EXPORT_SYMBOL(ioread32_rep);

void iowrite8_rep(void __iomem *addr, const void *src, unsigned long count)
{
	mmio_outsb(addr, src, count);
}
EXPORT_SYMBOL(iowrite8_rep);

void iowrite16_rep(void __iomem *addr, const void *src, unsigned long count)
{
	mmio_outsw(addr, src, count);
}
EXPORT_SYMBOL(iowrite16_rep);

void iowrite32_rep(void __iomem *addr, const void *src, unsigned long count)
{
	mmio_outsl(addr, src, count);
}
EXPORT_SYMBOL(iowrite32_rep);
