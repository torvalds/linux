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
#include <linux/bootmem.h>
#include <linux/pagemap.h>
#include <linux/poison.h>
#include <linux/gfp.h>

#include <asm/sections.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/vaddrs.h>
#include <asm/pgalloc.h>	/* bug in asm-generic/tlb.h: check_pgt_cache */
#include <asm/setup.h>
#include <asm/tlb.h>
#include <asm/prom.h>
#include <asm/leon.h>

#include "mm_32.h"

unsigned long *sparc_valid_addr_bitmap;
EXPORT_SYMBOL(sparc_valid_addr_bitmap);

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

unsigned long __init bootmem_init(unsigned long *pages_avail)
{
	unsigned long bootmap_size, start_pfn;
	unsigned long end_of_phys_memory = 0UL;
	unsigned long bootmap_pfn, bytes_avail, size;
	int i;

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
					sp_banks[i+1].num_bytes = 0;
					sp_banks[i+1].base_addr = 0xdeadbeef;
				}
				break;
			}
		}
	}

	/* Start with page aligned address of last symbol in kernel
	 * image.  
	 */
	start_pfn  = (unsigned long)__pa(PAGE_ALIGN((unsigned long) &_end));

	/* Now shift down to get the real physical page frame number. */
	start_pfn >>= PAGE_SHIFT;

	bootmap_pfn = start_pfn;

	max_pfn = end_of_phys_memory >> PAGE_SHIFT;

	max_low_pfn = max_pfn;
	highstart_pfn = highend_pfn = max_pfn;

	if (max_low_pfn > pfn_base + (SRMMU_MAXMEM >> PAGE_SHIFT)) {
		highstart_pfn = pfn_base + (SRMMU_MAXMEM >> PAGE_SHIFT);
		max_low_pfn = calc_max_low_pfn();
		printk(KERN_NOTICE "%ldMB HIGHMEM available.\n",
		    calc_highpages() >> (20 - PAGE_SHIFT));
	}

#ifdef CONFIG_BLK_DEV_INITRD
	/* Now have to check initial ramdisk, so that bootmap does not overwrite it */
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
		}
		if (initrd_start) {
			if (initrd_start >= (start_pfn << PAGE_SHIFT) &&
			    initrd_start < (start_pfn << PAGE_SHIFT) + 2 * PAGE_SIZE)
				bootmap_pfn = PAGE_ALIGN (initrd_end) >> PAGE_SHIFT;
		}
	}
#endif	
	/* Initialize the boot-time allocator. */
	bootmap_size = init_bootmem_node(NODE_DATA(0), bootmap_pfn, pfn_base,
					 max_low_pfn);

	/* Now register the available physical memory with the
	 * allocator.
	 */
	*pages_avail = 0;
	for (i = 0; sp_banks[i].num_bytes != 0; i++) {
		unsigned long curr_pfn, last_pfn;

		curr_pfn = sp_banks[i].base_addr >> PAGE_SHIFT;
		if (curr_pfn >= max_low_pfn)
			break;

		last_pfn = (sp_banks[i].base_addr + sp_banks[i].num_bytes) >> PAGE_SHIFT;
		if (last_pfn > max_low_pfn)
			last_pfn = max_low_pfn;

		/*
		 * .. finally, did all the rounding and playing
		 * around just make the area go away?
		 */
		if (last_pfn <= curr_pfn)
			continue;

		size = (last_pfn - curr_pfn) << PAGE_SHIFT;
		*pages_avail += last_pfn - curr_pfn;

		free_bootmem(sp_banks[i].base_addr, size);
	}

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start) {
		/* Reserve the initrd image area. */
		size = initrd_end - initrd_start;
		reserve_bootmem(initrd_start, size, BOOTMEM_DEFAULT);
		*pages_avail -= PAGE_ALIGN(size) >> PAGE_SHIFT;

		initrd_start = (initrd_start - phys_base) + PAGE_OFFSET;
		initrd_end = (initrd_end - phys_base) + PAGE_OFFSET;		
	}
#endif
	/* Reserve the kernel text/data/bss. */
	size = (start_pfn << PAGE_SHIFT) - phys_base;
	reserve_bootmem(phys_base, size, BOOTMEM_DEFAULT);
	*pages_avail -= PAGE_ALIGN(size) >> PAGE_SHIFT;

	/* Reserve the bootmem map.   We do not account for it
	 * in pages_avail because we will release that memory
	 * in free_all_bootmem.
	 */
	size = bootmap_size;
	reserve_bootmem((bootmap_pfn << PAGE_SHIFT), size, BOOTMEM_DEFAULT);
	*pages_avail -= PAGE_ALIGN(size) >> PAGE_SHIFT;

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
		__alloc_bootmem(i << 2, SMP_CACHE_BYTES, 0UL);

	if (sparc_valid_addr_bitmap == NULL) {
		prom_printf("mem_init: Cannot alloc valid_addr_bitmap.\n");
		prom_halt();
	}
	memset(sparc_valid_addr_bitmap, 0, i << 2);

	taint_real_pages();

	max_mapnr = last_valid_pfn - pfn_base;
	high_memory = __va(max_low_pfn << PAGE_SHIFT);
	free_all_bootmem();

	for (i = 0; sp_banks[i].num_bytes != 0; i++) {
		unsigned long start_pfn = sp_banks[i].base_addr >> PAGE_SHIFT;
		unsigned long end_pfn = (sp_banks[i].base_addr + sp_banks[i].num_bytes) >> PAGE_SHIFT;

		if (end_pfn <= highstart_pfn)
			continue;

		if (start_pfn < highstart_pfn)
			start_pfn = highstart_pfn;

		map_high_region(start_pfn, end_pfn);
	}
	
	mem_init_print_info(NULL);
}

void free_initmem (void)
{
	free_initmem_default(POISON_FREE_INITMEM);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, POISON_FREE_INITMEM,
			   "initrd");
}
#endif

void sparc_flush_page_to_ram(struct page *page)
{
	unsigned long vaddr = (unsigned long)page_address(page);

	if (vaddr)
		__flush_page_to_ram(vaddr);
}
EXPORT_SYMBOL(sparc_flush_page_to_ram);
