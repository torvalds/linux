/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Kevin Cernekee <cernekee@gmail.com>
 */

#define pr_fmt(fmt)		"bmips-dma: " fmt

#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/dma-direct.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <asm/bmips.h>

/*
 * BCM338x has configurable address translation windows which allow the
 * peripherals' DMA addresses to be different from the Zephyr-visible
 * physical addresses.  e.g. usb_dma_addr = zephyr_pa ^ 0x08000000
 *
 * If the "brcm,ubus" node has a "dma-ranges" property we will enable this
 * translation globally using the provided information.  This implements a
 * very limited subset of "dma-ranges" support and it will probably be
 * replaced by a more generic version later.
 */

struct bmips_dma_range {
	u32			child_addr;
	u32			parent_addr;
	u32			size;
};

static struct bmips_dma_range *bmips_dma_ranges;

#define FLUSH_RAC		0x100

dma_addr_t phys_to_dma(struct device *dev, phys_addr_t pa)
{
	struct bmips_dma_range *r;

	for (r = bmips_dma_ranges; r && r->size; r++) {
		if (pa >= r->child_addr &&
		    pa < (r->child_addr + r->size))
			return pa - r->child_addr + r->parent_addr;
	}
	return pa;
}

phys_addr_t dma_to_phys(struct device *dev, dma_addr_t dma_addr)
{
	struct bmips_dma_range *r;

	for (r = bmips_dma_ranges; r && r->size; r++) {
		if (dma_addr >= r->parent_addr &&
		    dma_addr < (r->parent_addr + r->size))
			return dma_addr - r->parent_addr + r->child_addr;
	}
	return dma_addr;
}

void arch_sync_dma_for_cpu_all(void)
{
	void __iomem *cbr = BMIPS_GET_CBR();
	u32 cfg;

	if (boot_cpu_type() != CPU_BMIPS3300 &&
	    boot_cpu_type() != CPU_BMIPS4350 &&
	    boot_cpu_type() != CPU_BMIPS4380)
		return;

	/* Flush stale data out of the readahead cache */
	cfg = __raw_readl(cbr + BMIPS_RAC_CONFIG);
	__raw_writel(cfg | 0x100, cbr + BMIPS_RAC_CONFIG);
	__raw_readl(cbr + BMIPS_RAC_CONFIG);
}

static int __init bmips_init_dma_ranges(void)
{
	struct device_node *np =
		of_find_compatible_node(NULL, NULL, "brcm,ubus");
	const __be32 *data;
	struct bmips_dma_range *r;
	int len;

	if (!np)
		return 0;

	data = of_get_property(np, "dma-ranges", &len);
	if (!data)
		goto out_good;

	len /= sizeof(*data) * 3;
	if (!len)
		goto out_bad;

	/* add a dummy (zero) entry at the end as a sentinel */
	bmips_dma_ranges = kcalloc(len + 1, sizeof(struct bmips_dma_range),
				   GFP_KERNEL);
	if (!bmips_dma_ranges)
		goto out_bad;

	for (r = bmips_dma_ranges; len; len--, r++) {
		r->child_addr = be32_to_cpup(data++);
		r->parent_addr = be32_to_cpup(data++);
		r->size = be32_to_cpup(data++);
	}

out_good:
	of_node_put(np);
	return 0;

out_bad:
	pr_err("error parsing dma-ranges property\n");
	of_node_put(np);
	return -EINVAL;
}
arch_initcall(bmips_init_dma_ranges);
