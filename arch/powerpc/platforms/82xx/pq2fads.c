/*
 * PQ2FADS board support
 *
 * Copyright 2007 Freescale Semiconductor, Inc.
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * Loosely based on mp82xx ADS support by Vitaly Bordug <vbordug@ru.mvista.com>
 * Copyright (c) 2006 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/fsl_devices.h>

#include <asm/io.h>
#include <asm/cpm2.h>
#include <asm/udbg.h>
#include <asm/machdep.h>
#include <asm/of_platform.h>
#include <asm/time.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/cpm2_pic.h>

#include "pq2ads.h"
#include "pq2.h"

static void __init pq2fads_pic_init(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, "fsl,cpm2-pic");
	if (!np) {
		printk(KERN_ERR "PIC init: can not find fsl,cpm2-pic node\n");
		return;
	}

	cpm2_pic_init(np);
	of_node_put(np);

	/* Initialize stuff for the 82xx CPLD IC and install demux  */
	pq2ads_pci_init_irq();
}

struct cpm_pin {
	int port, pin, flags;
};

static struct cpm_pin pq2fads_pins[] = {
	/* SCC1 */
	{3, 30, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{3, 31, CPM_PIN_INPUT | CPM_PIN_PRIMARY},

	/* SCC2 */
	{3, 27, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{3, 28, CPM_PIN_INPUT | CPM_PIN_PRIMARY},

	/* FCC2 */
	{1, 18, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 19, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 20, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 21, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 22, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 23, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 24, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 25, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 26, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 27, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 28, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 29, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{1, 30, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 31, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{2, 18, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 19, CPM_PIN_INPUT | CPM_PIN_PRIMARY},

	/* FCC3 */
	{1, 4, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 5, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 6, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 7, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 8, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 9, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 10, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 11, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 12, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 13, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 14, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 15, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{1, 16, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{1, 17, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 16, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 17, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
};

static void __init init_ioports(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pq2fads_pins); i++) {
		struct cpm_pin *pin = &pq2fads_pins[i];
		cpm2_set_pin(pin->port, pin->pin, pin->flags);
	}

	cpm2_clk_setup(CPM_CLK_SCC1, CPM_BRG1, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_SCC1, CPM_BRG1, CPM_CLK_TX);
	cpm2_clk_setup(CPM_CLK_SCC2, CPM_BRG2, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_SCC2, CPM_BRG2, CPM_CLK_TX);
	cpm2_clk_setup(CPM_CLK_FCC2, CPM_CLK13, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_FCC2, CPM_CLK14, CPM_CLK_TX);
	cpm2_clk_setup(CPM_CLK_FCC3, CPM_CLK15, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_FCC3, CPM_CLK16, CPM_CLK_TX);
}

static void __init pq2fads_setup_arch(void)
{
	struct device_node *np;
	__be32 __iomem *bcsr;

	if (ppc_md.progress)
		ppc_md.progress("pq2fads_setup_arch()", 0);

	cpm2_reset();

	np = of_find_compatible_node(NULL, NULL, "fsl,pq2fads-bcsr");
	if (!np) {
		printk(KERN_ERR "No fsl,pq2fads-bcsr in device tree\n");
		return;
	}

	bcsr = of_iomap(np, 0);
	if (!bcsr) {
		printk(KERN_ERR "Cannot map BCSR registers\n");
		return;
	}

	of_node_put(np);

	/* Enable the serial and ethernet ports */

	clrbits32(&bcsr[1], BCSR1_RS232_EN1 | BCSR1_RS232_EN2 | BCSR1_FETHIEN);
	setbits32(&bcsr[1], BCSR1_FETH_RST);

	clrbits32(&bcsr[3], BCSR3_FETHIEN2);
	setbits32(&bcsr[3], BCSR3_FETH2_RST);

	iounmap(bcsr);

	init_ioports();

	/* Enable external IRQs */
	clrbits32(&cpm2_immr->im_siu_conf.siu_82xx.sc_siumcr, 0x0c000000);

	pq2_init_pci();

	if (ppc_md.progress)
		ppc_md.progress("pq2fads_setup_arch(), finish", 0);
}

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init pq2fads_probe(void)
{
	unsigned long root = of_get_flat_dt_root();
	return of_flat_dt_is_compatible(root, "fsl,pq2fads");
}

static struct of_device_id __initdata of_bus_ids[] = {
	{ .name = "soc", },
	{ .name = "cpm", },
	{ .name = "localbus", },
	{},
};

static int __init declare_of_platform_devices(void)
{
	if (!machine_is(pq2fads))
		return 0;

	/* Publish the QE devices */
	of_platform_bus_probe(NULL, of_bus_ids, NULL);
	return 0;
}
device_initcall(declare_of_platform_devices);

define_machine(pq2fads)
{
	.name = "Freescale PQ2FADS",
	.probe = pq2fads_probe,
	.setup_arch = pq2fads_setup_arch,
	.init_IRQ = pq2fads_pic_init,
	.get_irq = cpm2_get_irq,
	.calibrate_decr = generic_calibrate_decr,
	.restart = pq2_restart,
	.progress = udbg_progress,
};
