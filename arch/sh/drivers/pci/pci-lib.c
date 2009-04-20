#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>

unsigned long PCIBIOS_MIN_IO = 0x0000;
unsigned long PCIBIOS_MIN_MEM = 0;

/*
 * We need to avoid collisions with `mirrored' VGA ports
 * and other strange ISA hardware, so we always want the
 * addresses to be allocated in the 0x000-0x0ff region
 * modulo 0x400.
 */
void pcibios_align_resource(void *data, struct resource *res,
			    resource_size_t size, resource_size_t align)
{
	struct pci_dev *dev = data;
	struct pci_channel *chan = dev->sysdata;
	resource_size_t start = res->start;

	if (res->flags & IORESOURCE_IO) {
		if (start < PCIBIOS_MIN_IO + chan->io_resource->start)
			start = PCIBIOS_MIN_IO + chan->io_resource->start;

		/*
                 * Put everything into 0x00-0xff region modulo 0x400.
		 */
		if (start & 0x300) {
			start = (start + 0x3ff) & ~0x3ff;
			res->start = start;
		}
	} else if (res->flags & IORESOURCE_MEM) {
		if (start < PCIBIOS_MIN_MEM + chan->mem_resource->start)
			start = PCIBIOS_MIN_MEM + chan->mem_resource->start;
	}

	res->start = start;
}

int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state, int write_combine)
{
	/*
	 * I/O space can be accessed via normal processor loads and stores on
	 * this platform but for now we elect not to do this and portable
	 * drivers should not do this anyway.
	 */
	if (mmap_state == pci_mmap_io)
		return -EINVAL;

	/*
	 * Ignore write-combine; for now only return uncached mappings.
	 */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

static void __iomem *ioport_map_pci(struct pci_dev *dev,
				    unsigned long port, unsigned int nr)
{
	struct pci_channel *chan = dev->sysdata;

	if (!chan->io_map_base)
		chan->io_map_base = generic_io_base;

	return (void __iomem *)(chan->io_map_base + port);
}

void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	resource_size_t start = pci_resource_start(dev, bar);
	resource_size_t len = pci_resource_len(dev, bar);
	unsigned long flags = pci_resource_flags(dev, bar);

	if (unlikely(!len || !start))
		return NULL;
	if (maxlen && len > maxlen)
		len = maxlen;

	if (flags & IORESOURCE_IO)
		return ioport_map_pci(dev, start, len);

	/*
	 * Presently the IORESOURCE_MEM case is a bit special, most
	 * SH7751 style PCI controllers have PCI memory at a fixed
	 * location in the address space where no remapping is desired.
	 * With the IORESOURCE_MEM case more care has to be taken
	 * to inhibit page table mapping for legacy cores, but this is
	 * punted off to __ioremap().
	 *					-- PFM.
	 */
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
	iounmap(addr);
}
EXPORT_SYMBOL(pci_iounmap);

#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL(pcibios_resource_to_bus);
EXPORT_SYMBOL(pcibios_bus_to_resource);
EXPORT_SYMBOL(PCIBIOS_MIN_IO);
EXPORT_SYMBOL(PCIBIOS_MIN_MEM);
#endif
