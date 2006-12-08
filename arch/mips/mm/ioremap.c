/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 * (C) Copyright 2001, 2002 Ralf Baechle
 */
#include <linux/mm.h>
#include <linux/module.h>
#include <asm/addrspace.h>
#include <asm/byteorder.h>

#include <linux/vmalloc.h>
#include <linux/io.h>

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

#define IS_LOW512(addr) (!((phys_t)(addr) & (phys_t) ~0x1fffffffULL))

void __iomem * __ioremap(phys_t phys_addr, phys_t size, unsigned long flags)
{
	struct vm_struct * area;
	unsigned long offset;
	phys_t last_addr;
	void * addr;
	pgprot_t pgprot;

	phys_addr = fixup_bigphys_addr(phys_addr, size);

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr)
		return NULL;

	/*
	 * Map uncached objects in the low 512mb of address space using KSEG1,
	 * otherwise map using page tables.
	 */
	if (IS_LOW512(phys_addr) && IS_LOW512(last_addr) &&
	    flags == _CACHE_UNCACHED)
		return (void __iomem *) CKSEG1ADDR(phys_addr);

	/*
	 * Don't allow anybody to remap normal RAM that we're using..
	 */
	if (phys_addr < virt_to_phys(high_memory)) {
		char *t_addr, *t_end;
		struct page *page;

		t_addr = __va(phys_addr);
		t_end = t_addr + (size - 1);

		for(page = virt_to_page(t_addr); page <= virt_to_page(t_end); page++)
			if(!PageReserved(page))
				return NULL;
	}

	pgprot = __pgprot(_PAGE_GLOBAL | _PAGE_PRESENT | __READABLE
			  | __WRITEABLE | flags);

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
	if (ioremap_page_range((unsigned long)addr, (unsigned long)addr + size,
			       phys_addr, pgprot)) {
		vunmap(addr);
		return NULL;
	}

	return (void __iomem *) (offset + (char *)addr);
}

#define IS_KSEG1(addr) (((unsigned long)(addr) & ~0x1fffffffUL) == CKSEG1)

void __iounmap(const volatile void __iomem *addr)
{
	struct vm_struct *p;

	if (IS_KSEG1(addr))
		return;

	p = remove_vm_area((void *) (PAGE_MASK & (unsigned long __force) addr));
	if (!p)
		printk(KERN_ERR "iounmap: bad address %p\n", addr);

        kfree(p);
}

EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(__iounmap);
