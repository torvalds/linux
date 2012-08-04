/*
 * Copyright (C) 2010-2011 Samsung Electronics Co.Ltd
 *
 * Base S5P MFC resource and device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/memblock.h>
#include <linux/ioport.h>

#include <mach/map.h>
#include <plat/devs.h>
#include <plat/irqs.h>
#include <plat/mfc.h>

struct s5p_mfc_reserved_mem {
	phys_addr_t	base;
	unsigned long	size;
	struct device	*dev;
};

static struct s5p_mfc_reserved_mem s5p_mfc_mem[2] __initdata;

void __init s5p_mfc_reserve_mem(phys_addr_t rbase, unsigned int rsize,
				phys_addr_t lbase, unsigned int lsize)
{
	int i;

	s5p_mfc_mem[0].dev = &s5p_device_mfc_r.dev;
	s5p_mfc_mem[0].base = rbase;
	s5p_mfc_mem[0].size = rsize;

	s5p_mfc_mem[1].dev = &s5p_device_mfc_l.dev;
	s5p_mfc_mem[1].base = lbase;
	s5p_mfc_mem[1].size = lsize;

	for (i = 0; i < ARRAY_SIZE(s5p_mfc_mem); i++) {
		struct s5p_mfc_reserved_mem *area = &s5p_mfc_mem[i];
		if (memblock_remove(area->base, area->size)) {
			printk(KERN_ERR "Failed to reserve memory for MFC device (%ld bytes at 0x%08lx)\n",
			       area->size, (unsigned long) area->base);
			area->base = 0;
		}
	}
}

static int __init s5p_mfc_memory_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(s5p_mfc_mem); i++) {
		struct s5p_mfc_reserved_mem *area = &s5p_mfc_mem[i];
		if (!area->base)
			continue;

		if (dma_declare_coherent_memory(area->dev, area->base,
				area->base, area->size,
				DMA_MEMORY_MAP | DMA_MEMORY_EXCLUSIVE) == 0)
			printk(KERN_ERR "Failed to declare coherent memory for MFC device (%ld bytes at 0x%08lx)\n",
			       area->size, (unsigned long) area->base);
	}
	return 0;
}
device_initcall(s5p_mfc_memory_init);
