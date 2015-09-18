#ifndef __VIRT_CONVERT__
#define __VIRT_CONVERT__

/*
 * Macros used for converting between virtual and physical mappings.
 */

#ifdef __KERNEL__

#include <linux/compiler.h>
#include <linux/mmzone.h>
#include <asm/setup.h>
#include <asm/page.h>

/*
 * Change virtual addresses to physical addresses and vv.
 */
static inline unsigned long virt_to_phys(void *address)
{
	return __pa(address);
}

static inline void *phys_to_virt(unsigned long address)
{
	return __va(address);
}

/* Permanent address of a page. */
#if defined(CONFIG_MMU) && defined(CONFIG_SINGLE_MEMORY_CHUNK)
#define page_to_phys(page) \
	__pa(PAGE_OFFSET + (((page) - pg_data_map[0].node_mem_map) << PAGE_SHIFT))
#else
#define page_to_phys(page)	(page_to_pfn(page) << PAGE_SHIFT)
#endif

/*
 * IO bus memory addresses are 1:1 with the physical address,
 */
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

#endif
#endif
