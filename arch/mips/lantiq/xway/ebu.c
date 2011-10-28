/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  EBU - the external bus unit attaches PCI, NOR and NAND
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/ioport.h>

#include <lantiq_soc.h>

/* all access to the ebu must be locked */
DEFINE_SPINLOCK(ebu_lock);
EXPORT_SYMBOL_GPL(ebu_lock);

static struct resource ltq_ebu_resource = {
	.name	= "ebu",
	.start	= LTQ_EBU_BASE_ADDR,
	.end	= LTQ_EBU_BASE_ADDR + LTQ_EBU_SIZE - 1,
	.flags	= IORESOURCE_MEM,
};

/* remapped base addr of the clock unit and external bus unit */
void __iomem *ltq_ebu_membase;

static int __init lantiq_ebu_init(void)
{
	/* insert and request the memory region */
	if (insert_resource(&iomem_resource, &ltq_ebu_resource) < 0)
		panic("Failed to insert ebu memory\n");

	if (request_mem_region(ltq_ebu_resource.start,
			resource_size(&ltq_ebu_resource), "ebu") < 0)
		panic("Failed to request ebu memory\n");

	/* remap ebu register range */
	ltq_ebu_membase = ioremap_nocache(ltq_ebu_resource.start,
				resource_size(&ltq_ebu_resource));
	if (!ltq_ebu_membase)
		panic("Failed to remap ebu memory\n");

	/* make sure to unprotect the memory region where flash is located */
	ltq_ebu_w32(ltq_ebu_r32(LTQ_EBU_BUSCON0) & ~EBU_WRDIS, LTQ_EBU_BUSCON0);
	return 0;
}

postcore_initcall(lantiq_ebu_init);
