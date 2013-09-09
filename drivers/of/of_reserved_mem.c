/*
 * Device tree based initialization code for reserved memory.
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Author: Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License or (at your optional) any later version of the license.
 */

#include <asm/dma-contiguous.h>

#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/mm.h>
#include <linux/sizes.h>
#include <linux/mm_types.h>
#include <linux/dma-contiguous.h>
#include <linux/dma-mapping.h>
#include <linux/of_reserved_mem.h>

#define MAX_RESERVED_REGIONS	16
struct reserved_mem {
	phys_addr_t		base;
	unsigned long		size;
	struct cma		*cma;
	char			name[32];
};
static struct reserved_mem reserved_mem[MAX_RESERVED_REGIONS];
static int reserved_mem_count;

static int __init fdt_scan_reserved_mem(unsigned long node, const char *uname,
					int depth, void *data)
{
	struct reserved_mem *rmem = &reserved_mem[reserved_mem_count];
	phys_addr_t base, size;
	int is_cma, is_reserved;
	unsigned long len;
	const char *status;
	__be32 *prop;

	is_cma = IS_ENABLED(CONFIG_DMA_CMA) &&
	       of_flat_dt_is_compatible(node, "linux,contiguous-memory-region");
	is_reserved = of_flat_dt_is_compatible(node, "reserved-memory-region");

	if (!is_reserved && !is_cma) {
		/* ignore node and scan next one */
		return 0;
	}

	status = of_get_flat_dt_prop(node, "status", &len);
	if (status && strcmp(status, "okay") != 0) {
		/* ignore disabled node nad scan next one */
		return 0;
	}

	prop = of_get_flat_dt_prop(node, "reg", &len);
	if (!prop || (len < (dt_root_size_cells + dt_root_addr_cells) *
			     sizeof(__be32))) {
		pr_err("Reserved mem: node %s, incorrect \"reg\" property\n",
		       uname);
		/* ignore node and scan next one */
		return 0;
	}
	base = dt_mem_next_cell(dt_root_addr_cells, &prop);
	size = dt_mem_next_cell(dt_root_size_cells, &prop);

	if (!size) {
		/* ignore node and scan next one */
		return 0;
	}

	pr_info("Reserved mem: found %s, memory base %lx, size %ld MiB\n",
		uname, (unsigned long)base, (unsigned long)size / SZ_1M);

	if (reserved_mem_count == ARRAY_SIZE(reserved_mem))
		return -ENOSPC;

	rmem->base = base;
	rmem->size = size;
	strlcpy(rmem->name, uname, sizeof(rmem->name));

	if (is_cma) {
		struct cma *cma;
		if (dma_contiguous_reserve_area(size, base, 0, &cma) == 0) {
			rmem->cma = cma;
			reserved_mem_count++;
			if (of_get_flat_dt_prop(node,
						"linux,default-contiguous-region",
						NULL))
				dma_contiguous_set_default(cma);
		}
	} else if (is_reserved) {
		if (memblock_remove(base, size) == 0)
			reserved_mem_count++;
		else
			pr_err("Failed to reserve memory for %s\n", uname);
	}

	return 0;
}

static struct reserved_mem *get_dma_memory_region(struct device *dev)
{
	struct device_node *node;
	const char *name;
	int i;

	node = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!node)
		return NULL;

	name = kbasename(node->full_name);
	for (i = 0; i < reserved_mem_count; i++)
		if (strcmp(name, reserved_mem[i].name) == 0)
			return &reserved_mem[i];
	return NULL;
}

/**
 * of_reserved_mem_device_init() - assign reserved memory region to given device
 *
 * This function assign memory region pointed by "memory-region" device tree
 * property to the given device.
 */
void of_reserved_mem_device_init(struct device *dev)
{
	struct reserved_mem *region = get_dma_memory_region(dev);
	if (!region)
		return;

	if (region->cma) {
		dev_set_cma_area(dev, region->cma);
		pr_info("Assigned CMA %s to %s device\n", region->name,
			dev_name(dev));
	} else {
		if (dma_declare_coherent_memory(dev, region->base, region->base,
		    region->size, DMA_MEMORY_MAP | DMA_MEMORY_EXCLUSIVE) != 0)
			pr_info("Declared reserved memory %s to %s device\n",
				region->name, dev_name(dev));
	}
}

/**
 * of_reserved_mem_device_release() - release reserved memory device structures
 *
 * This function releases structures allocated for memory region handling for
 * the given device.
 */
void of_reserved_mem_device_release(struct device *dev)
{
	struct reserved_mem *region = get_dma_memory_region(dev);
	if (!region && !region->cma)
		dma_release_declared_memory(dev);
}

/**
 * early_init_dt_scan_reserved_mem() - create reserved memory regions
 *
 * This function grabs memory from early allocator for device exclusive use
 * defined in device tree structures. It should be called by arch specific code
 * once the early allocator (memblock) has been activated and all other
 * subsystems have already allocated/reserved memory.
 */
void __init early_init_dt_scan_reserved_mem(void)
{
	of_scan_flat_dt_by_path("/memory/reserved-memory",
				fdt_scan_reserved_mem, NULL);
}
