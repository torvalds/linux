/*
 * Copyright (C) 2007,2008 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Author: John Rigby <jrigby@freescale.com>
 *
 * Description:
 * MPC512x Shared code
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of_platform.h>

#include <asm/machdep.h>
#include <asm/ipic.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <asm/mpc5121.h>

#include "mpc512x.h"

static struct mpc512x_reset_module __iomem *reset_module_base;

static void __init mpc512x_restart_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-reset");
	if (!np)
		return;

	reset_module_base = of_iomap(np, 0);
	of_node_put(np);
}

void mpc512x_restart(char *cmd)
{
	if (reset_module_base) {
		/* Enable software reset "RSTE" */
		out_be32(&reset_module_base->rpr, 0x52535445);
		/* Set software hard reset */
		out_be32(&reset_module_base->rcr, 0x2);
	} else {
		pr_err("Restart module not mapped.\n");
	}
	for (;;)
		;
}

void __init mpc512x_init_IRQ(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-ipic");
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
 * Nodes to do bus probe on, soc and localbus
 */
static struct of_device_id __initdata of_bus_ids[] = {
	{ .compatible = "fsl,mpc5121-immr", },
	{ .compatible = "fsl,mpc5121-localbus", },
	{},
};

void __init mpc512x_declare_of_platform_devices(void)
{
	struct device_node *np;

	if (of_platform_bus_probe(NULL, of_bus_ids, NULL))
		printk(KERN_ERR __FILE__ ": "
			"Error while probing of_platform bus\n");

	np = of_find_compatible_node(NULL, NULL, "fsl,mpc5121-nfc");
	if (np) {
		of_platform_device_create(np, NULL, NULL);
		of_node_put(np);
	}
}

void __init mpc512x_init(void)
{
	mpc512x_declare_of_platform_devices();
	mpc5121_clk_init();
	mpc512x_restart_init();
}
