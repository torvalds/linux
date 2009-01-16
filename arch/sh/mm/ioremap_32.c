/*
 * arch/sh/mm/ioremap.c
 *
 * Re-map IO memory to kernel address space so that we can access it.
 * This is needed for high PCI addresses that aren't mapped in the
 * 640k-1MB IO memory area on PC's
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 * (C) Copyright 2005, 2006 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 */
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/addrspace.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/mmu.h>

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
void __iomem *__ioremap(unsigned long phys_addr, unsigned long size,
			unsigned long flags)
{
	struct vm_struct * area;
	unsigned long offset, last_addr, addr, orig_addr;
	pgprot_t pgprot;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * If we're on an SH7751 or SH7780 PCI controller, PCI memory is
	 * mapped at the end of the address space (typically 0xfd000000)
	 * in a non-translatable area, so mapping through page tables for
	 * this area is not only pointless, but also fundamentally
	 * broken. Just return the physical address instead.
	 *
	 * For boards that map a small PCI memory aperture somewhere in
	 * P1/P2 space, ioremap() will already do the right thing,
	 * and we'll never get this far.
	 */
	if (is_pci_memaddr(phys_addr) && is_pci_memaddr(last_addr))
		return (void __iomem *)phys_addr;

	/*
	 * Don't allow anybody to remap normal RAM that we're using..
	 */
	if (phys_addr < virt_to_phys(high_memory))
		return NULL;

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr+1) - phys_addr;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	area->phys_addr = phys_addr;
	orig_addr = addr = (unsigned long)area->addr;

#ifdef CONFIG_32BIT
	/*
	 * First try to remap through the PMB once a valid VMA has been
	 * established. Smaller allocations (or the rest of the size
	 * remaining after a PMB mapping due to the size not being
	 * perfectly aligned on a PMB size boundary) are then mapped
	 * through the UTLB using conventional page tables.
	 *
	 * PMB entries are all pre-faulted.
	 */
	if (unlikely(size >= 0x1000000)) {
		unsigned long mapped = pmb_remap(addr, phys_addr, size, flags);

		if (likely(mapped)) {
			addr		+= mapped;
			phys_addr	+= mapped;
			size		-= mapped;
		}
	}
#endif

	pgprot = __pgprot(pgprot_val(PAGE_KERNEL_NOCACHE) | flags);
	if (likely(size))
		if (ioremap_page_range(addr, addr + size, phys_addr, pgprot)) {
			vunmap((void *)orig_addr);
			return NULL;
		}

	return (void __iomem *)(offset + (char *)orig_addr);
}
EXPORT_SYMBOL(__ioremap);

void __iounmap(void __iomem *addr)
{
	unsigned long vaddr = (unsigned long __force)addr;
	unsigned long seg = PXSEG(vaddr);
	struct vm_struct *p;

	if (seg < P3SEG || seg >= P3_ADDR_MAX || is_pci_memaddr(vaddr))
		return;

#ifdef CONFIG_32BIT
	/*
	 * Purge any PMB entries that may have been established for this
	 * mapping, then proceed with conventional VMA teardown.
	 *
	 * XXX: Note that due to the way that remove_vm_area() does
	 * matching of the resultant VMA, we aren't able to fast-forward
	 * the address past the PMB space until the end of the VMA where
	 * the page tables reside. As such, unmap_vm_area() will be
	 * forced to linearly scan over the area until it finds the page
	 * tables where PTEs that need to be unmapped actually reside,
	 * which is far from optimal. Perhaps we need to use a separate
	 * VMA for the PMB mappings?
	 *					-- PFM.
	 */
	pmb_unmap(vaddr);
#endif

	p = remove_vm_area((void *)(vaddr & PAGE_MASK));
	if (!p) {
		printk(KERN_ERR "%s: bad address %p\n", __func__, addr);
		return;
	}

	kfree(p);
}
EXPORT_SYMBOL(__iounmap);
