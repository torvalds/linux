/*
 * ppc64 "iomap" interface implementation.
 *
 * (C) Copyright 2004 Linus Torvalds
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/export.h>
#include <asm/io.h>
#include <asm/pci-bridge.h>

/*
 * Here comes the ppc64 implementation of the IOMAP 
 * interfaces.
 */
unsigned int ioread8(void __iomem *addr)
{
	return readb(addr);
}
unsigned int ioread16(void __iomem *addr)
{
	return readw(addr);
}
unsigned int ioread16be(void __iomem *addr)
{
	return in_be16(addr);
}
unsigned int ioread32(void __iomem *addr)
{
	return readl(addr);
}
unsigned int ioread32be(void __iomem *addr)
{
	return in_be32(addr);
}
EXPORT_SYMBOL(ioread8);
EXPORT_SYMBOL(ioread16);
EXPORT_SYMBOL(ioread16be);
EXPORT_SYMBOL(ioread32);
EXPORT_SYMBOL(ioread32be);

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
	out_be16(addr, val);
}
void iowrite32(u32 val, void __iomem *addr)
{
	writel(val, addr);
}
void iowrite32be(u32 val, void __iomem *addr)
{
	out_be32(addr, val);
}
EXPORT_SYMBOL(iowrite8);
EXPORT_SYMBOL(iowrite16);
EXPORT_SYMBOL(iowrite16be);
EXPORT_SYMBOL(iowrite32);
EXPORT_SYMBOL(iowrite32be);

/*
 * These are the "repeat read/write" functions. Note the
 * non-CPU byte order. We do things in "IO byteorder"
 * here.
 *
 * FIXME! We could make these do EEH handling if we really
 * wanted. Not clear if we do.
 */
void ioread8_rep(void __iomem *addr, void *dst, unsigned long count)
{
	_insb((u8 __iomem *) addr, dst, count);
}
void ioread16_rep(void __iomem *addr, void *dst, unsigned long count)
{
	_insw_ns((u16 __iomem *) addr, dst, count);
}
void ioread32_rep(void __iomem *addr, void *dst, unsigned long count)
{
	_insl_ns((u32 __iomem *) addr, dst, count);
}
EXPORT_SYMBOL(ioread8_rep);
EXPORT_SYMBOL(ioread16_rep);
EXPORT_SYMBOL(ioread32_rep);

void iowrite8_rep(void __iomem *addr, const void *src, unsigned long count)
{
	_outsb((u8 __iomem *) addr, src, count);
}
void iowrite16_rep(void __iomem *addr, const void *src, unsigned long count)
{
	_outsw_ns((u16 __iomem *) addr, src, count);
}
void iowrite32_rep(void __iomem *addr, const void *src, unsigned long count)
{
	_outsl_ns((u32 __iomem *) addr, src, count);
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
void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long max)
{
	resource_size_t start = pci_resource_start(dev, bar);
	resource_size_t len = pci_resource_len(dev, bar);
	unsigned long flags = pci_resource_flags(dev, bar);

	if (!len)
		return NULL;
	if (max && len > max)
		len = max;
	if (flags & IORESOURCE_IO)
		return ioport_map(start, len);
	if (flags & IORESOURCE_MEM)
		return ioremap(start, len);
	/* What? */
	return NULL;
}

void pci_iounmap(struct pci_dev *dev, void __iomem *addr)
{
	if (isa_vaddr_is_ioport(addr))
		return;
	if (pcibios_vaddr_is_ioport(addr))
		return;
	iounmap(addr);
}

EXPORT_SYMBOL(pci_iomap);
EXPORT_SYMBOL(pci_iounmap);
#endif /* CONFIG_PCI */
