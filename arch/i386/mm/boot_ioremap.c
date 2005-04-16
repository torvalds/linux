/*
 * arch/i386/mm/boot_ioremap.c
 * 
 * Re-map functions for early boot-time before paging_init() when the 
 * boot-time pagetables are still in use
 *
 * Written by Dave Hansen <haveblue@us.ibm.com>
 */


/*
 * We need to use the 2-level pagetable functions, but CONFIG_X86_PAE
 * keeps that from happenning.  If anyone has a better way, I'm listening.
 *
 * boot_pte_t is defined only if this all works correctly
 */

#include <linux/config.h>
#undef CONFIG_X86_PAE
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <linux/init.h>
#include <linux/stddef.h>

/* 
 * I'm cheating here.  It is known that the two boot PTE pages are 
 * allocated next to each other.  I'm pretending that they're just
 * one big array. 
 */

#define BOOT_PTE_PTRS (PTRS_PER_PTE*2)
#define boot_pte_index(address) \
	     (((address) >> PAGE_SHIFT) & (BOOT_PTE_PTRS - 1))

static inline boot_pte_t* boot_vaddr_to_pte(void *address)
{
	boot_pte_t* boot_pg = (boot_pte_t*)pg0;
	return &boot_pg[boot_pte_index((unsigned long)address)];
}

/*
 * This is only for a caller who is clever enough to page-align
 * phys_addr and virtual_source, and who also has a preference
 * about which virtual address from which to steal ptes
 */
static void __boot_ioremap(unsigned long phys_addr, unsigned long nrpages, 
		    void* virtual_source)
{
	boot_pte_t* pte;
	int i;
	char *vaddr = virtual_source;

	pte = boot_vaddr_to_pte(virtual_source);
	for (i=0; i < nrpages; i++, phys_addr += PAGE_SIZE, pte++) {
		set_pte(pte, pfn_pte(phys_addr>>PAGE_SHIFT, PAGE_KERNEL));
		__flush_tlb_one(&vaddr[i*PAGE_SIZE]);
	}
}

/* the virtual space we're going to remap comes from this array */
#define BOOT_IOREMAP_PAGES 4
#define BOOT_IOREMAP_SIZE (BOOT_IOREMAP_PAGES*PAGE_SIZE)
static __initdata char boot_ioremap_space[BOOT_IOREMAP_SIZE]
		       __attribute__ ((aligned (PAGE_SIZE)));

/*
 * This only applies to things which need to ioremap before paging_init()
 * bt_ioremap() and plain ioremap() are both useless at this point.
 * 
 * When used, we're still using the boot-time pagetables, which only
 * have 2 PTE pages mapping the first 8MB
 *
 * There is no unmap.  The boot-time PTE pages aren't used after boot.
 * If you really want the space back, just remap it yourself.
 * boot_ioremap(&ioremap_space-PAGE_OFFSET, BOOT_IOREMAP_SIZE)
 */
__init void* boot_ioremap(unsigned long phys_addr, unsigned long size)
{
	unsigned long last_addr, offset;
	unsigned int nrpages;
	
	last_addr = phys_addr + size - 1;

	/* page align the requested address */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr) - phys_addr;
	
	nrpages = size >> PAGE_SHIFT;
	if (nrpages > BOOT_IOREMAP_PAGES)
		return NULL;
	
	__boot_ioremap(phys_addr, nrpages, boot_ioremap_space);

	return &boot_ioremap_space[offset];
}
