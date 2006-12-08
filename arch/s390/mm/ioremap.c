/*
 *  arch/s390/mm/ioremap.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *
 *  Derived from "arch/i386/mm/extable.c"
 *    (C) Copyright 1995 1996 Linus Torvalds
 *
 * Re-map IO memory to kernel address space so that we can access it.
 * This is needed for high PCI addresses that aren't mapped in the
 * 640k-1MB IO memory area on PC's
 */

#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <asm/pgalloc.h>

/*
 * Generic mapping function (not visible outside):
 */

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 */
void * __ioremap(unsigned long phys_addr, unsigned long size, unsigned long flags)
{
	void * addr;
	struct vm_struct * area;

	if (phys_addr < virt_to_phys(high_memory))
		return phys_to_virt(phys_addr);
	if (phys_addr & ~PAGE_MASK)
		return NULL;
	size = PAGE_ALIGN(size);
	if (!size || size > phys_addr + size)
		return NULL;
	area = get_vm_area(size, VM_IOREMAP);
	if (!area)
		return NULL;
	addr = area->addr;
	if (ioremap_page_range((unsigned long)addr, (unsigned long)addr + size,
			       phys_addr, __pgprot(flags))) {
		vfree(addr);
		return NULL;
	}
	return addr;
}

void iounmap(void *addr)
{
	if (addr > high_memory)
		vfree(addr);
}
