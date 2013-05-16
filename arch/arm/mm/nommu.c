/*
 *  linux/arch/arm/mm/nommu.c
 *
 * ARM uCLinux supporting functions.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/io.h>
#include <linux/memblock.h>

#include <asm/cacheflush.h>
#include <asm/sections.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/traps.h>
#include <asm/mach/arch.h>

#include "mm.h"

void __init arm_mm_memblock_reserve(void)
{
	/*
	 * Register the exception vector page.
	 * some architectures which the DRAM is the exception vector to trap,
	 * alloc_page breaks with error, although it is not NULL, but "0."
	 */
	memblock_reserve(CONFIG_VECTORS_BASE, PAGE_SIZE);
}

void __init sanity_check_meminfo(void)
{
	phys_addr_t end = bank_phys_end(&meminfo.bank[meminfo.nr_banks - 1]);
	high_memory = __va(end - 1) + 1;
}

/*
 * paging_init() sets up the page tables, initialises the zone memory
 * maps, and sets up the zero page, bad page and bad page tables.
 */
void __init paging_init(struct machine_desc *mdesc)
{
	early_trap_init((void *)CONFIG_VECTORS_BASE);
	bootmem_init();
}

/*
 * We don't need to do anything here for nommu machines.
 */
void setup_mm_for_reboot(void)
{
}

void flush_dcache_page(struct page *page)
{
	__cpuc_flush_dcache_area(page_address(page), PAGE_SIZE);
}
EXPORT_SYMBOL(flush_dcache_page);

void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
		       unsigned long uaddr, void *dst, const void *src,
		       unsigned long len)
{
	memcpy(dst, src, len);
	if (vma->vm_flags & VM_EXEC)
		__cpuc_coherent_user_range(uaddr, uaddr + len);
}

void __iomem *__arm_ioremap_pfn(unsigned long pfn, unsigned long offset,
				size_t size, unsigned int mtype)
{
	if (pfn >= (0x100000000ULL >> PAGE_SHIFT))
		return NULL;
	return (void __iomem *) (offset + (pfn << PAGE_SHIFT));
}
EXPORT_SYMBOL(__arm_ioremap_pfn);

void __iomem *__arm_ioremap_pfn_caller(unsigned long pfn, unsigned long offset,
			   size_t size, unsigned int mtype, void *caller)
{
	return __arm_ioremap_pfn(pfn, offset, size, mtype);
}

void __iomem *__arm_ioremap(phys_addr_t phys_addr, size_t size,
			    unsigned int mtype)
{
	return (void __iomem *)phys_addr;
}
EXPORT_SYMBOL(__arm_ioremap);

void __iomem * (*arch_ioremap_caller)(phys_addr_t, size_t, unsigned int, void *);

void __iomem *__arm_ioremap_caller(phys_addr_t phys_addr, size_t size,
				   unsigned int mtype, void *caller)
{
	return __arm_ioremap(phys_addr, size, mtype);
}

void (*arch_iounmap)(volatile void __iomem *);

void __arm_iounmap(volatile void __iomem *addr)
{
}
EXPORT_SYMBOL(__arm_iounmap);
