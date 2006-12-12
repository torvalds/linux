/*
 * arch/cris/mm/ioremap.c
 *
 * Re-map IO memory to kernel address space so that we can access it.
 * Needed for memory-mapped I/O devices mapped outside our normal DRAM
 * window (that is, all memory-mapped I/O devices).
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 * CRIS-port by Axis Communications AB
 */

#include <linux/vmalloc.h>
#include <linux/io.h>
#include <asm/pgalloc.h>
#include <asm/arch/memmap.h>

/*
 * Generic mapping function (not visible outside):
 */

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
void __iomem * __ioremap_prot(unsigned long phys_addr, unsigned long size, pgprot_t prot)
{
	void __iomem * addr;
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
	size = PAGE_ALIGN(last_addr+1) - phys_addr;

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	addr = (void __iomem *)area->addr;
	if (ioremap_page_range((unsigned long)addr, (unsigned long)addr + size,
			       phys_addr, prot)) {
		vfree((void __force *)addr);
		return NULL;
	}
	return (void __iomem *) (offset + (char __iomem *)addr);
}

void __iomem * __ioremap(unsigned long phys_addr, unsigned long size, unsigned long flags)
{
	return __ioremap_prot(phys_addr, size,
		              __pgprot(_PAGE_PRESENT | __READABLE |
				       __WRITEABLE | _PAGE_GLOBAL |
				       _PAGE_KERNEL | flags));
}

/**
 * ioremap_nocache     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * Must be freed with iounmap.
 */

void __iomem *ioremap_nocache (unsigned long phys_addr, unsigned long size)
{
        return __ioremap(phys_addr | MEM_NON_CACHEABLE, size, 0);
}

void iounmap(volatile void __iomem *addr)
{
	if (addr > high_memory)
		return vfree((void *) (PAGE_MASK & (unsigned long) addr));
}
