// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/sparc/mm/init.c
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1995 Eddie C. Dost (ecd@skynet.be)
 *  Copyright (C) 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *  Copyright (C) 2000 Anton Blanchard (anton@samba.org)
 */

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/initrd.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/memblock.h>
#include <linux/pagemap.h>
#include <linux/poison.h>
#include <linux/gfp.h>

#include <asm/sections.h>
#include <asm/page.h>
#include <asm/vaddrs.h>
#include <asm/setup.h>
#include <asm/tlb.h>
#include <asm/prom.h>
#include <asm/leon.h>

#include "mm_32.h"

static unsigned long *sparc_valid_addr_bitmap;

unsigned long phys_base;
EXPORT_SYMBOL(phys_base);

unsigned long pfn_base;
EXPORT_SYMBOL(pfn_base);

struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS+1];

/* Initial ramdisk setup */
extern unsigned int sparc_ramdisk_image;
extern unsigned int sparc_ramdisk_size;

unsigned long highstart_pfn, highend_pfn;

unsigned long last_valid_pfn;

unsigned long calc_highpages(void)
{
	int i;
	int nr = 0;

	for (i = 0; sp_banks[i].num_bytes != 0; i++) {
		unsigned long start_pfn = sp_banks[i].base_addr >> PAGE_SHIFT;
		unsigned long end_pfn = (sp_banks[i].base_addr + sp_banks[i].num_bytes) >> PAGE_SHIFT;

		if (end_pfn <= max_low_pfn)
			continue;

		if (start_pfn < max_low_pfn)
			start_pfn = max_low_pfn;

		nr += end_pfn - start_pfn;
	}

	return nr;
}

static unsigned long calc_max_low_pfn(void)
{
	int i;
	unsigned long tmp = pfn_base + (SRMMU_MAXMEM >> PAGE_SHIFT);
	unsigned long curr_pfn, last_pfn;

	last_pfn = (sp_banks[0].base_addr + sp_banks[0].num_bytes) >> PAGE_SHIFT;
	for (i = 1; sp_banks[i].num_bytes != 0; i++) {
		curr_pfn = sp_banks[i].base_addr >> PAGE_SHIFT;

		if (curr_pfn >= tmp) {
			if (last_pfn < tmp)
				tmp = last_pfn;
			break;
		}

		last_pfn = (sp_banks[i].base_addr + sp_banks[i].num_bytes) >> PAGE_SHIFT;
	}

	return tmp;
}

static void __init find_ramdisk(unsigned long end_of_phys_memory)
{
#ifdef CONFIG_BLK_DEV_INITRD
	unsigned long size;

	/* Now have to check initial ramdisk, so that it won't pass
	 * the end of memory
	 */
	if (sparc_ramdisk_image) {
		if (sparc_ramdisk_image >= (unsigned long)&_end - 2 * PAGE_SIZE)
			sparc_ramdisk_image -= KERNBASE;
		initrd_start = sparc_ramdisk_image + phys_base;
		initrd_end = initrd_start + sparc_ramdisk_size;
		if (initrd_end > end_of_phys_memory) {
			printk(KERN_CRIT "initrd extends beyond end of memory "
			       "(0x%016lx > 0x%016lx)\ndisabling initrd\n",
			       initrd_end, end_of_phys_memory);
			initrd_start = 0;
		} else {
			/* Reserve the initrd image area. */
			size = initrd_end - initrd_start;
			memblock_reserve(initrd_start, size);

			initrd_start = (initrd_start - phys_base) + PAGE_OFFSET;
			initrd_end = (initrd_end - phys_base) + PAGE_OFFSET;
		}
	}
#endif
}

unsigned long __init bootmem_init(unsigned long *pages_avail)
{
	unsigned long start_pfn, bytes_avail, size;
	unsigned long end_of_phys_memory = 0;
	unsigned long high_pages = 0;
	int i;

	memblock_set_bottom_up(true);
	memblock_allow_resize();

	bytes_avail = 0UL;
	for (i = 0; sp_banks[i].num_bytes != 0; i++) {
		end_of_phys_memory = sp_banks[i].base_addr +
			sp_banks[i].num_bytes;
		bytes_avail += sp_banks[i].num_bytes;
		if (cmdline_memory_size) {
			if (bytes_avail > cmdline_memory_size) {
				unsigned long slack = bytes_avail - cmdline_memory_size;

				bytes_avail -= slack;
				end_of_phys_memory -= slack;

				sp_banks[i].num_bytes -= slack;
				if (sp_banks[i].num_bytes == 0) {
					sp_banks[i].base_addr = 0xdeadbeef;
				} else {
					memblock_add(sp_banks[i].base_addr,
						     sp_banks[i].num_bytes);
					sp_banks[i+1].num_bytes = 0;
					sp_banks[i+1].base_addr = 0xdeadbeef;
				}
				break;
			}
		}
		memblock_add(sp_banks[i].base_addr, sp_banks[i].num_bytes);
	}

	/* Start with page aligned address of last symbol in kernel
	 * image.
	 */
	start_pfn  = (unsigned long)__pa(PAGE_ALIGN((unsigned long) &_end));

	/* Now shift down to get the real physical page frame number. */
	start_pfn >>= PAGE_SHIFT;

	max_pfn = end_of_phys_memory >> PAGE_SHIFT;

	max_low_pfn = max_pfn;
	highstart_pfn = highend_pfn = max_pfn;

	if (max_low_pfn > pfn_base + (SRMMU_MAXMEM >> PAGE_SHIFT)) {
		highstart_pfn = pfn_base + (SRMMU_MAXMEM >> PAGE_SHIFT);
		max_low_pfn = calc_max_low_pfn();
		high_pages = calc_highpages();
		printk(KERN_NOTICE "%ldMB HIGHMEM available.\n",
		    high_pages >> (20 - PAGE_SHIFT));
	}

	find_ramdisk(end_of_phys_memory);

	/* Reserve the kernel text/data/bss. */
	size = (start_pfn << PAGE_SHIFT) - phys_base;
	memblock_reserve(phys_base, size);
	memblock_add(phys_base, size);

	size = memblock_phys_mem_size() - memblock_reserved_size();
	*pages_avail = (size >> PAGE_SHIFT) - high_pages;

	/* Only allow low memory to be allocated via memblock allocation */
	memblock_set_current_limit(max_low_pfn << PAGE_SHIFT);

	return max_pfn;
}

/*
 * paging_init() sets up the page tables: We call the MMU specific
 * init routine based upon the Sun model type on the Sparc.
 *
 */
void __init paging_init(void)
{
	srmmu_paging_init();
	prom_build_devicetree();
	of_fill_in_cpu_data();
	device_scan();
}

static void __init taint_real_pages(void)
{
	int i;

	for (i = 0; sp_banks[i].num_bytes; i++) {
		unsigned long start, end;

		start = sp_banks[i].base_addr;
		end = start + sp_banks[i].num_bytes;

		while (start < end) {
			set_bit(start >> 20, sparc_valid_addr_bitmap);
			start += PAGE_SIZE;
		}
	}
}

static void map_high_region(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long tmp;

#ifdef CONFIG_DEBUG_HIGHMEM
	printk("mapping high region %08lx - %08lx\n", start_pfn, end_pfn);
#endif

	for (tmp = start_pfn; tmp < end_pfn; tmp++)
		free_highmem_page(pfn_to_page(tmp));
}

void __init mem_init(void)
{
	int i;

	if (PKMAP_BASE+LAST_PKMAP*PAGE_SIZE >= FIXADDR_START) {
		prom_printf("BUG: fixmap and pkmap areas overlap\n");
		prom_printf("pkbase: 0x%lx pkend: 0x%lx fixstart 0x%lx\n",
		       PKMAP_BASE,
		       (unsigned long)PKMAP_BASE+LAST_PKMAP*PAGE_SIZE,
		       FIXADDR_START);
		prom_printf("Please mail sparclinux@vger.kernel.org.\n");
		prom_halt();
	}


	/* Saves us work later. */
	memset((void *)empty_zero_page, 0, PAGE_SIZE);

	i = last_valid_pfn >> ((20 - PAGE_SHIFT) + 5);
	i += 1;
	sparc_valid_addr_bitmap = (unsigned long *)
		memblock_alloc(i << 2, SMP_CACHE_BYTES);

	if (sparc_valid_addr_bitmap == NULL) {
		prom_printf("mem_init: Cannot alloc valid_addr_bitmap.\n");
		prom_halt();
	}
	memset(sparc_valid_addr_bitmap, 0, i << 2);

	taint_real_pages();

	max_mapnr = last_valid_pfn - pfn_base;
	high_memory = __va(max_low_pfn << PAGE_SHIFT);
	memblock_free_all();

	for (i = 0; sp_banks[i].num_bytes != 0; i++) {
		unsigned long start_pfn = sp_banks[i].base_addr >> PAGE_SHIFT;
		unsigned long end_pfn = (sp_banks[i].base_addr + sp_banks[i].num_bytes) >> PAGE_SHIFT;

		if (end_pfn <= highstart_pfn)
			continue;

		if (start_pfn < highstart_pfn)
			start_pfn = highstart_pfn;

		map_high_region(start_pfn, end_pfn);
	}
}

void sparc_flush_page_to_ram(struct page *page)
{
	unsigned long vaddr = (unsigned long)page_address(page);

	__flush_page_to_ram(vaddr);
}
EXPORT_SYMBOL(sparc_flush_page_to_ram);

void sparc_flush_folio_to_ram(struct folio *folio)
{
	unsigned long vaddr = (unsigned long)folio_address(folio);
	unsigned int i, nr = folio_nr_pages(folio);

	for (i = 0; i < nr; i++)
		__flush_page_to_ram(vaddr + i * PAGE_SIZE);
}
EXPORT_SYMBOL(sparc_flush_folio_to_ram);

static const pgprot_t protection_map[16] = {
	[VM_NONE]					= PAGE_NONE,
	[VM_READ]					= PAGE_READONLY,
	[VM_WRITE]					= PAGE_COPY,
	[VM_WRITE | VM_READ]				= PAGE_COPY,
	[VM_EXEC]					= PAGE_READONLY,
	[VM_EXEC | VM_READ]				= PAGE_READONLY,
	[VM_EXEC | VM_WRITE]				= PAGE_COPY,
	[VM_EXEC | VM_WRITE | VM_READ]			= PAGE_COPY,
	[VM_SHARED]					= PAGE_NONE,
	[VM_SHARED | VM_READ]				= PAGE_READONLY,
	[VM_SHARED | VM_WRITE]				= PAGE_SHARED,
	[VM_SHARED | VM_WRITE | VM_READ]		= PAGE_SHARED,
	[VM_SHARED | VM_EXEC]				= PAGE_READONLY,
	[VM_SHARED | VM_EXEC | VM_READ]			= PAGE_READONLY,
	[VM_SHARED | VM_EXEC | VM_WRITE]		= PAGE_SHARED,
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= PAGE_SHARED
};
DECLARE_VM_GET_PAGE_PROT
