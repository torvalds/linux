/*
 *  linux/arch/arm/mm/nommu.c
 *
 * ARM uCLinux supporting functions.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>

#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/mach/arch.h>

#include "mm.h"

extern void _stext, __data_start, _end;

/*
 * Reserve the various regions of node 0
 */
void __init reserve_node_zero(pg_data_t *pgdat)
{
	/*
	 * Register the kernel text and data with bootmem.
	 * Note that this can only be in node 0.
	 */
#ifdef CONFIG_XIP_KERNEL
	reserve_bootmem_node(pgdat, __pa(&__data_start), &_end - &__data_start);
#else
	reserve_bootmem_node(pgdat, __pa(&_stext), &_end - &_stext);
#endif

	/*
	 * Register the exception vector page.
	 * some architectures which the DRAM is the exception vector to trap,
	 * alloc_page breaks with error, although it is not NULL, but "0."
	 */
	reserve_bootmem_node(pgdat, CONFIG_VECTORS_BASE, PAGE_SIZE);
}

/*
 * paging_init() sets up the page tables, initialises the zone memory
 * maps, and sets up the zero page, bad page and bad page tables.
 */
void __init paging_init(struct meminfo *mi, struct machine_desc *mdesc)
{
	bootmem_init(mi);
}

/*
 * We don't need to do anything here for nommu machines.
 */
void setup_mm_for_reboot(char mode)
{
}

void flush_dcache_page(struct page *page)
{
	__cpuc_flush_dcache_page(page_address(page));
}
EXPORT_SYMBOL(flush_dcache_page);

void __iomem *__ioremap_pfn(unsigned long pfn, unsigned long offset,
			    size_t size, unsigned long flags)
{
	if (pfn >= (0x100000000ULL >> PAGE_SHIFT))
		return NULL;
	return (void __iomem *) (offset + (pfn << PAGE_SHIFT));
}
EXPORT_SYMBOL(__ioremap_pfn);

void __iomem *__ioremap(unsigned long phys_addr, size_t size,
			unsigned long flags)
{
	return (void __iomem *)phys_addr;
}
EXPORT_SYMBOL(__ioremap);

void __iounmap(volatile void __iomem *addr)
{
}
EXPORT_SYMBOL(__iounmap);
