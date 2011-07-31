/*
 * arch/parisc/mm/ioremap.c
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 * (C) Copyright 2001-2006 Helge Deller <deller@gmx.de>
 * (C) Copyright 2005 Kyle McMartin <kyle@parisc-linux.org>
 */

#include <linux/vmalloc.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/io.h>
#include <asm/pgalloc.h>

/*
 * Generic mapping function (not visible outside):
 */

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
void __iomem * __ioremap(unsigned long phys_addr, unsigned long size, unsigned long flags)
{
	void __iomem *addr;
	struct vm_struct *area;
	unsigned long offset, last_addr;
	pgprot_t pgprot;

#ifdef CONFIG_EISA
	unsigned long end = phys_addr + size - 1;
	/* Support EISA addresses */
	if ((phys_addr >= 0x00080000 && end < 0x000fffff) ||
	    (phys_addr >= 0x00500000 && end < 0x03bfffff)) {
		phys_addr |= F_EXTEND(0xfc000000);
		flags |= _PAGE_NO_CACHE;
	}
#endif

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * Don't allow anybody to remap normal RAM that we're using..
	 */
	if (phys_addr < virt_to_phys(high_memory)) {
		char *t_addr, *t_end;
		struct page *page;

		t_addr = __va(phys_addr);
		t_end = t_addr + (size - 1);
	   
		for (page = virt_to_page(t_addr); 
		     page <= virt_to_page(t_end); page++) {
			if(!PageReserved(page))
				return NULL;
		}
	}

	pgprot = __pgprot(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY |
			  _PAGE_ACCESSED | flags);

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

	addr = (void __iomem *) area->addr;
	if (ioremap_page_range((unsigned long)addr, (unsigned long)addr + size,
			       phys_addr, pgprot)) {
		vfree(addr);
		return NULL;
	}

	return (void __iomem *) (offset + (char __iomem *)addr);
}
EXPORT_SYMBOL(__ioremap);

void iounmap(const volatile void __iomem *addr)
{
	if (addr > high_memory)
		return vfree((void *) (PAGE_MASK & (unsigned long __force) addr));
}
EXPORT_SYMBOL(iounmap);
