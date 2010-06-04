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
#include <asm/mpc52xx_psc.h>

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

#define DEFAULT_FIFO_SIZE 16

static unsigned int __init get_fifo_size(struct device_node *np,
					 char *prop_name)
{
	const unsigned int *fp;

	fp = of_get_property(np, prop_name, NULL);
	if (fp)
		return *fp;

	pr_warning("no %s property in %s node, defaulting to %d\n",
		   prop_name, np->full_name, DEFAULT_FIFO_SIZE);

	return DEFAULT_FIFO_SIZE;
}

#define FIFOC(_base) ((struct mpc512x_psc_fifo __iomem *) \
		    ((u32)(_base) + sizeof(struct mpc52xx_psc)))

/* Init PSC FIFO space for TX and RX slices */
void __init mpc512x_psc_fifo_init(void)
{
	struct device_node *np;
	void __iomem *psc;
	unsigned int tx_fifo_size;
	unsigned int rx_fifo_size;
	int fifobase = 0; /* current fifo address in 32 bit words */

	for_each_compatible_node(np, NULL, "fsl,mpc5121-psc") {
		tx_fifo_size = get_fifo_size(np, "fsl,tx-fifo-size");
		rx_fifo_size = get_fifo_size(np, "fsl,rx-fifo-size");

		/* size in register is in 4 byte units */
		tx_fifo_size /= 4;
		rx_fifo_size /= 4;
		if (!tx_fifo_size)
			tx_fifo_size = 1;
		if (!rx_fifo_size)
			rx_fifo_size = 1;

		psc = of_iomap(np, 0);
		if (!psc) {
			pr_err("%s: Can't map %s device\n",
				__func__, np->full_name);
			continue;
		}

		/* FIFO space is 4KiB, check if requested size is available */
		if ((fifobase + tx_fifo_size + rx_fifo_size) > 0x1000) {
			pr_err("%s: no fifo space available for %s\n",
				__func__, np->full_name);
			iounmap(psc);
			/*
			 * chances are that another device requests less
			 * fifo space, so we continue.
			 */
			continue;
		}

		/* set tx and rx fifo size registers */
		out_be32(&FIFOC(psc)->txsz, (fifobase << 16) | tx_fifo_size);
		fifobase += tx_fifo_size;
		out_be32(&FIFOC(psc)->rxsz, (fifobase << 16) | rx_fifo_size);
		fifobase += rx_fifo_size;

		/* reset and enable the slices */
		out_be32(&FIFOC(psc)->txcmd, 0x80);
		out_be32(&FIFOC(psc)->txcmd, 0x01);
		out_be32(&FIFOC(psc)->rxcmd, 0x80);
		out_be32(&FIFOC(psc)->rxcmd, 0x01);

		iounmap(psc);
	}
}

void __init mpc512x_init(void)
{
	mpc512x_declare_of_platform_devices();
	mpc5121_clk_init();
	mpc512x_restart_init();
	mpc512x_psc_fifo_init();
}
