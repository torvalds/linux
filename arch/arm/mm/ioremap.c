/*
 *  linux/arch/arm/mm/ioremap.c
 *
 * Re-map IO memory to kernel address space so that we can access it.
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 *
 * Hacked for ARM by Phil Blundell <philb@gnu.org>
 * Hacked to allow all architectures to build, and various cleanups
 * by Russell King
 *
 * This allows a driver to remap an arbitrary region of bus memory into
 * virtual space.  One should *only* use readl, writel, memcpy_toio and
 * so on with such remapped areas.
 *
 * Because the ARM only has a 32-bit address space we can't address the
 * whole of the (physical) PCI space at once.  PCI huge-mode addressing
 * allows us to circumvent this restriction by splitting PCI space into
 * two 2GB chunks and mapping only one at a time into processor memory.
 * We use MMU protection domains to trap any attempt to access the bank
 * that is not currently mapped.  (This isn't fully implemented yet.)
 */
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

#include <asm/cacheflush.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/tlbflush.h>

static inline void
remap_area_pte(pte_t * pte, unsigned long address, unsigned long size,
	       unsigned long phys_addr, pgprot_t pgprot)
{
	unsigned long end;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	BUG_ON(address >= end);
	do {
		if (!pte_none(*pte))
			goto bad;

		set_pte(pte, pfn_pte(phys_addr >> PAGE_SHIFT, pgprot));
		address += PAGE_SIZE;
		phys_addr += PAGE_SIZE;
		pte++;
	} while (address && (address < end));
	return;

 bad:
	printk("remap_area_pte: page already exists\n");
	BUG();
}

static inline int
remap_area_pmd(pmd_t * pmd, unsigned long address, unsigned long size,
	       unsigned long phys_addr, unsigned long flags)
{
	unsigned long end;
	pgprot_t pgprot;

	address &= ~PGDIR_MASK;
	end = address + size;

	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;

	phys_addr -= address;
	BUG_ON(address >= end);

	pgprot = __pgprot(L_PTE_PRESENT | L_PTE_YOUNG | L_PTE_DIRTY | L_PTE_WRITE | flags);
	do {
		pte_t * pte = pte_alloc_kernel(pmd, address);
		if (!pte)
			return -ENOMEM;
		remap_area_pte(pte, address, end - address, address + phys_addr, pgprot);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return 0;
}

static int
remap_area_pages(unsigned long start, unsigned long phys_addr,
		 unsigned long size, unsigned long flags)
{
	unsigned long address = start;
	unsigned long end = start + size;
	int err = 0;
	pgd_t * dir;

	phys_addr -= address;
	dir = pgd_offset(&init_mm, address);
	BUG_ON(address >= end);
	do {
		pmd_t *pmd = pmd_alloc(&init_mm, dir, address);
		if (!pmd) {
			err = -ENOMEM;
			break;
		}
		if (remap_area_pmd(pmd, address, end - address,
					 phys_addr + address, flags)) {
			err = -ENOMEM;
			break;
		}

		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	} while (address && (address < end));

	flush_cache_vmap(start, end);
	return err;
}

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 *
 * 'flags' are the extra L_PTE_ flags that you want to specify for this
 * mapping.  See include/asm-arm/proc-armv/pgtable.h for more information.
 */
void __iomem *
__ioremap(unsigned long phys_addr, size_t size, unsigned long flags,
	  unsigned long align)
{
	void * addr;
	struct vm_struct * area;
	unsigned long offset, last_addr;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr + 1) - phys_addr;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	addr = area->addr;
	if (remap_area_pages((unsigned long) addr, phys_addr, size, flags)) {
		vfree(addr);
		return NULL;
	}
	return (void __iomem *) (offset + (char *)addr);
}
EXPORT_SYMBOL(__ioremap);

void __iounmap(void __iomem *addr)
{
	vfree((void *) (PAGE_MASK & (unsigned long) addr));
}
EXPORT_SYMBOL(__iounmap);

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
#include <linux/pci.h>
#include <linux/ioport.h>

void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	unsigned long start = pci_resource_start(dev, bar);
	unsigned long len   = pci_resource_len(dev, bar);
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
