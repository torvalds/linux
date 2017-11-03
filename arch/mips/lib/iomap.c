// SPDX-License-Identifier: GPL-2.0
/*
 * Implement the default iomap interfaces
 *
 * (C) Copyright 2004 Linus Torvalds
 * (C) Copyright 2006 Ralf Baechle <ralf@linux-mips.org>
 * (C) Copyright 2007 MIPS Technologies, Inc.
 *     written by Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/export.h>
#include <asm/io.h>

/*
 * Read/write from/to an (offsettable) iomem cookie. It might be a PIO
 * access or a MMIO access, these functions don't care. The info is
 * encoded in the hardware mapping set up by the mapping functions
 * (or the cookie itself, depending on implementation and hw).
 *
 * The generic routines don't assume any hardware mappings, and just
 * encode the PIO/MMIO as part of the cookie. They coldly assume that
 * the MMIO IO mappings are not in the low address range.
 *
 * Architectures for which this is not true can't use this generic
 * implementation and should do their own copy.
 */

#define PIO_MASK	0x0ffffUL

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
 * Note the "__mem" accesses, since we want to convert
 * to CPU byte order if the host bus happens to not match the
 * endianness of PCI/ISA (see mach-generic/mangle-port.h).
 */
static inline void mmio_insb(void __iomem *addr, u8 *dst, int count)
{
	while (--count >= 0) {
		u8 data = __mem_readb(addr);
		*dst = data;
		dst++;
	}
}

static inline void mmio_insw(void __iomem *addr, u16 *dst, int count)
{
	while (--count >= 0) {
		u16 data = __mem_readw(addr);
		*dst = data;
		dst++;
	}
}

static inline void mmio_insl(void __iomem *addr, u32 *dst, int count)
{
	while (--count >= 0) {
		u32 data = __mem_readl(addr);
		*dst = data;
		dst++;
	}
}

static inline void mmio_outsb(void __iomem *addr, const u8 *src, int count)
{
	while (--count >= 0) {
		__mem_writeb(*src, addr);
		src++;
	}
}

static inline void mmio_outsw(void __iomem *addr, const u16 *src, int count)
{
	while (--count >= 0) {
		__mem_writew(*src, addr);
		src++;
	}
}

static inline void mmio_outsl(void __iomem *addr, const u32 *src, int count)
{
	while (--count >= 0) {
		__mem_writel(*src, addr);
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

/*
 * Create a virtual mapping cookie for an IO port range
 *
 * This uses the same mapping are as the in/out family which has to be setup
 * by the platform initialization code.
 *
 * Just to make matters somewhat more interesting on MIPS systems with
 * multiple host bridge each will have it's own ioport address space.
 */
static void __iomem *ioport_map_legacy(unsigned long port, unsigned int nr)
{
	return (void __iomem *) (mips_io_port_base + port);
}

void __iomem *ioport_map(unsigned long port, unsigned int nr)
{
	if (port > PIO_MASK)
		return NULL;

	return ioport_map_legacy(port, nr);
}

EXPORT_SYMBOL(ioport_map);

void ioport_unmap(void __iomem *addr)
{
	/* Nothing to do */
}

EXPORT_SYMBOL(ioport_unmap);
