/*
 * ATSTK1000 board-specific setup code.
 *
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/linkage.h>

#include <asm/setup.h>

#include <asm/arch/board.h>

/* Initialized by bootloader-specific startup code. */
struct tag *bootloader_tags __initdata;

struct lcdc_platform_data __initdata atstk1000_fb0_data;

void __init board_setup_fbmem(unsigned long fbmem_start,
			      unsigned long fbmem_size)
{
	if (!fbmem_size)
		return;

	if (!fbmem_start) {
		void *fbmem;

		fbmem = alloc_bootmem_low_pages(fbmem_size);
		fbmem_start = __pa(fbmem);
	} else {
		pg_data_t *pgdat;

		for_each_online_pgdat(pgdat) {
			if (fbmem_start >= pgdat->bdata->node_boot_start
			    && fbmem_start <= pgdat->bdata->node_low_pfn)
				reserve_bootmem_node(pgdat, fbmem_start,
						     fbmem_size);
		}
	}

	printk("%luKiB framebuffer memory at address 0x%08lx\n",
	       fbmem_size >> 10, fbmem_start);
	atstk1000_fb0_data.fbmem_start = fbmem_start;
	atstk1000_fb0_data.fbmem_size = fbmem_size;
}
