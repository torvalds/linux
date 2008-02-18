/*
 * Copyright (C) 2007 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: John Rigby, <jrigby@freescale.com>, Thur Mar 29 2007
 *
 * Description:
 * MPC5121 ADS board setup
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of_platform.h>

#include <asm/machdep.h>
#include <asm/ipic.h>
#include <asm/prom.h>
#include <asm/time.h>

/**
 * 	mpc512x_find_ips_freq - Find the IPS bus frequency for a device
 * 	@node:	device node
 *
 * 	Returns IPS bus frequency, or 0 if the bus frequency cannot be found.
 */
unsigned long
mpc512x_find_ips_freq(struct device_node *node)
{
	struct device_node *np;
	const unsigned int *p_ips_freq = NULL;

	of_node_get(node);
	while (node) {
		p_ips_freq = of_get_property(node, "bus-frequency", NULL);
		if (p_ips_freq)
			break;

		np = of_get_parent(node);
		of_node_put(node);
		node = np;
	}
	if (node)
		of_node_put(node);

	return p_ips_freq ? *p_ips_freq : 0;
}
EXPORT_SYMBOL(mpc512x_find_ips_freq);

static struct of_device_id __initdata of_bus_ids[] = {
	{ .name = "soc", },
	{ .name = "localbus", },
	{},
};

static void __init mpc5121_ads_declare_of_platform_devices(void)
{
	/* Find every child of the SOC node and add it to of_platform */
	if (of_platform_bus_probe(NULL, of_bus_ids, NULL))
		printk(KERN_ERR __FILE__ ": "
			"Error while probing of_platform bus\n");
}

static void __init mpc5121_ads_init_IRQ(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,ipic");
	if (!np)
		return;

	ipic_init(np, 0);
	of_node_put(np);

	/*
	 * Initialize the default interrupt mapping priorities,
	 * in case the boot rom changed something on us.
	 */
	ipic_set_default_priority();
}

/*
 * Called very early, MMU is off, device-tree isn't unflattened
 */
static int __init mpc5121_ads_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "fsl,mpc5121ads");
}

define_machine(mpc5121_ads) {
	.name			= "MPC5121 ADS",
	.probe			= mpc5121_ads_probe,
	.init			= mpc5121_ads_declare_of_platform_devices,
	.init_IRQ		= mpc5121_ads_init_IRQ,
	.get_irq		= ipic_get_irq,
	.calibrate_decr		= generic_calibrate_decr,
};
