/*
 * Copyright (C) 2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdarg.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/ioport.h>

#include <asm/cacheflush.h>
#include <linux/of_address.h>

#include "bcm_kona_smc.h"

static u32		bcm_smc_buffer_phys;	/* physical address */
static void __iomem	*bcm_smc_buffer;	/* virtual address */

struct bcm_kona_smc_data {
	unsigned service_id;
	unsigned arg0;
	unsigned arg1;
	unsigned arg2;
	unsigned arg3;
};

static const struct of_device_id bcm_kona_smc_ids[] __initconst = {
	{.compatible = "brcm,kona-smc"},
	{.compatible = "bcm,kona-smc"}, /* deprecated name */
	{},
};

/* Map in the args buffer area */
int __init bcm_kona_smc_init(void)
{
	struct device_node *node;
	const __be32 *prop_val;
	u64 prop_size = 0;
	unsigned long buffer_size;
	u32 buffer_phys;

	/* Read buffer addr and size from the device tree node */
	node = of_find_matching_node(NULL, bcm_kona_smc_ids);
	if (!node)
		return -ENODEV;

	prop_val = of_get_address(node, 0, &prop_size, NULL);
	if (!prop_val)
		return -EINVAL;

	/* We assume space for four 32-bit arguments */
	if (prop_size < 4 * sizeof(u32) || prop_size > (u64)ULONG_MAX)
		return -EINVAL;
	buffer_size = (unsigned long)prop_size;

	buffer_phys = be32_to_cpup(prop_val);
	if (!buffer_phys)
		return -EINVAL;

	bcm_smc_buffer = ioremap(buffer_phys, buffer_size);
	if (!bcm_smc_buffer)
		return -ENOMEM;
	bcm_smc_buffer_phys = buffer_phys;

	pr_info("Kona Secure API initialized\n");

	return 0;
}

/* __bcm_kona_smc() should only run on CPU 0, with pre-emption disabled */
static void __bcm_kona_smc(void *info)
{
	struct bcm_kona_smc_data *data = info;
	u32 *args = bcm_smc_buffer;
	int rc;

	BUG_ON(smp_processor_id() != 0);
	BUG_ON(!args);

	/* Copy the four 32 bit argument values into the bounce area */
	writel_relaxed(data->arg0, args++);
	writel_relaxed(data->arg1, args++);
	writel_relaxed(data->arg2, args++);
	writel(data->arg3, args);

	/* Flush caches for input data passed to Secure Monitor */
	flush_cache_all();

	/* Trap into Secure Monitor */
	rc = bcm_kona_smc_asm(data->service_id, bcm_smc_buffer_phys);

	if (rc != SEC_ROM_RET_OK)
		pr_err("Secure Monitor call failed (0x%x)!\n", rc);
}

unsigned bcm_kona_smc(unsigned service_id, unsigned arg0, unsigned arg1,
		  unsigned arg2, unsigned arg3)
{
	struct bcm_kona_smc_data data;

	data.service_id = service_id;
	data.arg0 = arg0;
	data.arg1 = arg1;
	data.arg2 = arg2;
	data.arg3 = arg3;

	/*
	 * Due to a limitation of the secure monitor, we must use the SMP
	 * infrastructure to forward all secure monitor calls to Core 0.
	 */
	if (get_cpu() != 0)
		smp_call_function_single(0, __bcm_kona_smc, (void *)&data, 1);
	else
		__bcm_kona_smc(&data);

	put_cpu();

	return 0;
}
