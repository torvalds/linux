// SPDX-License-Identifier: GPL-2.0
/*
 * ppc64 "iomap" interface implementation.
 *
 * (C) Copyright 2004 Linus Torvalds
 */
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>
#include <asm/isa-bridge.h>

/*
 * Here comes the ppc64 implementation of the IOMAP 
 * interfaces.
 */
unsigned int ioread8(const void __iomem *addr)
{
	return readb(addr);
}
unsigned int ioread16(const void __iomem *addr)
{
	return readw(addr);
}
unsigned int ioread16be(const void __iomem *addr)
{
	return readw_be(addr);
}
unsigned int ioread32(const void __iomem *addr)
{
	return readl(addr);
}
unsigned int ioread32be(const void __iomem *addr)
{
	return readl_be(addr);
}
EXPORT_SYMBOL(ioread8);
EXPORT_SYMBOL(ioread16);
EXPORT_SYMBOL(ioread16be);
EXPORT_SYMBOL(ioread32);
EXPORT_SYMBOL(ioread32be);
#ifdef __powerpc64__
u64 ioread64(const void __iomem *addr)
{
	return readq(addr);
}
u64 ioread64_lo_hi(const void __iomem *addr)
{
	return readq(addr);
}
u64 ioread64_hi_lo(const void __iomem *addr)
{
	return readq(addr);
}
u64 ioread64be(const void __iomem *addr)
{
	return readq_be(addr);
}
u64 ioread64be_lo_hi(const void __iomem *addr)
{
	return readq_be(addr);
}
u64 ioread64be_hi_lo(const void __iomem *addr)
{
	return readq_be(addr);
}
EXPORT_SYMBOL(ioread64);
EXPORT_SYMBOL(ioread64_lo_hi);
EXPORT_SYMBOL(ioread64_hi_lo);
EXPORT_SYMBOL(ioread64be);
EXPORT_SYMBOL(ioread64be_lo_hi);
EXPORT_SYMBOL(ioread64be_hi_lo);
#endif /* __powerpc64__ */

void iowrite8(u8 val, void __iomem *addr)
{
	writeb(val, addr);
}
void iowrite16(u16 val, void __iomem *addr)
{
	writew(val, addr);
}
void iowrite16be(u16 val, void __iomem *addr)
{
	writew_be(val, addr);
}
void iowrite32(u32 val, void __iomem *addr)
{
	writel(val, addr);
}
void iowrite32be(u32 val, void __iomem *addr)
{
	writel_be(val, addr);
}
EXPORT_SYMBOL(iowrite8);
EXPORT_SYMBOL(iowrite16);
EXPORT_SYMBOL(iowrite16be);
EXPORT_SYMBOL(iowrite32);
EXPORT_SYMBOL(iowrite32be);
#ifdef __powerpc64__
void iowrite64(u64 val, void __iomem *addr)
{
	writeq(val, addr);
}
void iowrite64_lo_hi(u64 val, void __iomem *addr)
{
	writeq(val, addr);
}
void iowrite64_hi_lo(u64 val, void __iomem *addr)
{
	writeq(val, addr);
}
void iowrite64be(u64 val, void __iomem *addr)
{
	writeq_be(val, addr);
}
void iowrite64be_lo_hi(u64 val, void __iomem *addr)
{
	writeq_be(val, addr);
}
void iowrite64be_hi_lo(u64 val, void __iomem *addr)
{
	writeq_be(val, addr);
}
EXPORT_SYMBOL(iowrite64);
EXPORT_SYMBOL(iowrite64_lo_hi);
EXPORT_SYMBOL(iowrite64_hi_lo);
EXPORT_SYMBOL(iowrite64be);
EXPORT_SYMBOL(iowrite64be_lo_hi);
EXPORT_SYMBOL(iowrite64be_hi_lo);
#endif /* __powerpc64__ */

/*
 * These are the "repeat read/write" functions. Note the
 * non-CPU byte order. We do things in "IO byteorder"
 * here.
 *
 * FIXME! We could make these do EEH handling if we really
 * wanted. Not clear if we do.
 */
void ioread8_rep(const void __iomem *addr, void *dst, unsigned long count)
{
	readsb(addr, dst, count);
}
void ioread16_rep(const void __iomem *addr, void *dst, unsigned long count)
{
	readsw(addr, dst, count);
}
void ioread32_rep(const void __iomem *addr, void *dst, unsigned long count)
{
	readsl(addr, dst, count);
}
EXPORT_SYMBOL(ioread8_rep);
EXPORT_SYMBOL(ioread16_rep);
EXPORT_SYMBOL(ioread32_rep);

void iowrite8_rep(void __iomem *addr, const void *src, unsigned long count)
{
	writesb(addr, src, count);
}
void iowrite16_rep(void __iomem *addr, const void *src, unsigned long count)
{
	writesw(addr, src, count);
}
void iowrite32_rep(void __iomem *addr, const void *src, unsigned long count)
{
	writesl(addr, src, count);
}
EXPORT_SYMBOL(iowrite8_rep);
EXPORT_SYMBOL(iowrite16_rep);
EXPORT_SYMBOL(iowrite32_rep);

void __iomem *ioport_map(unsigned long port, unsigned int len)
{
	return (void __iomem *) (port + _IO_BASE);
}

void ioport_unmap(void __iomem *addr)
{
	/* Nothing to do */
}
EXPORT_SYMBOL(ioport_map);
EXPORT_SYMBOL(ioport_unmap);

#ifdef CONFIG_PCI
void pci_iounmap(struct pci_dev *dev, void __iomem *addr)
{
	if (isa_vaddr_is_ioport(addr))
		return;
	if (pcibios_vaddr_is_ioport(addr))
		return;
	iounmap(addr);
}

EXPORT_SYMBOL(pci_iounmap);
#endif /* CONFIG_PCI */
