/*
 *  linux/arch/arm/mm/iomap.c
 *
 * Map IO port and PCI memory spaces so that {read,write}[bwl] can
 * be used to access this memory.
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/io.h>

unsigned long vga_base;
EXPORT_SYMBOL(vga_base);

#ifdef __io
void __iomem *ioport_map(unsigned long port, unsigned int nr)
{
	return __io(port);
}
EXPORT_SYMBOL(ioport_map);

void ioport_unmap(void __iomem *addr)
{
}
EXPORT_SYMBOL(ioport_unmap);
#endif

#ifdef CONFIG_PCI
unsigned long pcibios_min_io = 0x1000;
EXPORT_SYMBOL(pcibios_min_io);

unsigned long pcibios_min_mem = 0x01000000;
EXPORT_SYMBOL(pcibios_min_mem);

unsigned int pci_flags = PCI_REASSIGN_ALL_RSRC;
EXPORT_SYMBOL(pci_flags);

void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	resource_size_t start = pci_resource_start(dev, bar);
	resource_size_t len   = pci_resource_len(dev, bar);
	unsigned long flags = pci_resource_flags(dev, bar);

	if (!len || !start)
		return NULL;
	if (maxlen && len > maxlen)
		len = maxlen;
	if (flags & IORESOURCE_IO)
		return ioport_map(start, len);
	if (flags & IORESOURCE_MEM) {
		if (flags & IORESOURCE_CACHEABLE)
			return ioremap(start, len);
		return ioremap_nocache(start, len);
	}
	return NULL;
}
EXPORT_SYMBOL(pci_iomap);

void pci_iounmap(struct pci_dev *dev, void __iomem *addr)
{
	if ((unsigned long)addr >= VMALLOC_START &&
	    (unsigned long)addr < VMALLOC_END)
		iounmap(addr);
}
EXPORT_SYMBOL(pci_iounmap);
#endif
