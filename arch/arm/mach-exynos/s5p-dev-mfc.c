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
#include <linux/of_fdt.h>
#include <linux/of.h>

static struct platform_device s5p_device_mfc_l;
static struct platform_device s5p_device_mfc_r;

struct s5p_mfc_dt_meminfo {
	unsigned long	loff;
	unsigned long	lsize;
	unsigned long	roff;
	unsigned long	rsize;
	char		*compatible;
};

struct s5p_mfc_reserved_mem {
	phys_addr_t	base;
	unsigned long	size;
	struct device	*dev;
};

static struct s5p_mfc_reserved_mem s5p_mfc_mem[2] __initdata;


static void __init s5p_mfc_reserve_mem(phys_addr_t rbase, unsigned int rsize,
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

int __init s5p_fdt_alloc_mfc_mem(unsigned long node, const char *uname,
				int depth, void *data)
{
	const __be32 *prop;
	int len;
	struct s5p_mfc_dt_meminfo mfc_mem;

	if (!data)
		return 0;

	if (!of_flat_dt_is_compatible(node, data))
		return 0;

	prop = of_get_flat_dt_prop(node, "samsung,mfc-l", &len);
	if (!prop || (len != 2 * sizeof(unsigned long)))
		return 0;

	mfc_mem.loff = be32_to_cpu(prop[0]);
	mfc_mem.lsize = be32_to_cpu(prop[1]);

	prop = of_get_flat_dt_prop(node, "samsung,mfc-r", &len);
	if (!prop || (len != 2 * sizeof(unsigned long)))
		return 0;

	mfc_mem.roff = be32_to_cpu(prop[0]);
	mfc_mem.rsize = be32_to_cpu(prop[1]);

	s5p_mfc_reserve_mem(mfc_mem.roff, mfc_mem.rsize,
			mfc_mem.loff, mfc_mem.lsize);

	return 1;
}
