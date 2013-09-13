/*
 * Re-map IO memory to kernel address space so that we can access it.
 * Needed for memory-mapped I/O devices mapped outside our normal DRAM
 * window (that is, all memory-mapped I/O devices).
 *
 * Copyright (C) 1995,1996 Linus Torvalds
 *
 * Meta port based on CRIS-port by Axis Communications AB
 */

#include <linux/vmalloc.h>
#include <linux/io.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/pgtable.h>

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
void __iomem *__ioremap(unsigned long phys_addr, size_t size,
			unsigned long flags)
{
	unsigned long addr;
	struct vm_struct *area;
	unsigned long offset, last_addr;
	pgprot_t prot;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/* Custom region addresses are accessible and uncached by default. */
	if (phys_addr >= LINSYSCUSTOM_BASE &&
	    phys_addr < (LINSYSCUSTOM_BASE + LINSYSCUSTOM_LIMIT))
		return (__force void __iomem *) phys_addr;

	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr+1) - phys_addr;
	prot = __pgprot(_PAGE_PRESENT | _PAGE_WRITE | _PAGE_DIRTY |
			_PAGE_ACCESSED | _PAGE_KERNEL | _PAGE_CACHE_WIN0 |
			flags);

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	area->phys_addr = phys_addr;
	addr = (unsigned long) area->addr;
	if (ioremap_page_range(addr, addr + size, phys_addr, prot)) {
		vunmap((void *) addr);
		return NULL;
	}
	return (__force void __iomem *) (offset + (char *)addr);
}
EXPORT_SYMBOL(__ioremap);

void __iounmap(void __iomem *addr)
{
	struct vm_struct *p;

	if ((__force unsigned long)addr >= LINSYSCUSTOM_BASE &&
	    (__force unsigned long)addr < (LINSYSCUSTOM_BASE +
					   LINSYSCUSTOM_LIMIT))
		return;

	p = remove_vm_area((void *)(PAGE_MASK & (unsigned long __force)addr));
	if (unlikely(!p)) {
		pr_err("iounmap: bad address %p\n", addr);
		return;
	}

	kfree(p);
}
EXPORT_SYMBOL(__iounmap);
