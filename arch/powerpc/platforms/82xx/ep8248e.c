/*
 * Embedded Planet EP8248E support
 *
 * Copyright 2007 Freescale Semiconductor, Inc.
 * Author: Scott Wood <scottwood@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/fsl_devices.h>
#include <linux/mdio-bitbang.h>
#include <linux/of_mdio.h>
#include <linux/slab.h>
#include <linux/of_platform.h>

#include <asm/io.h>
#include <asm/cpm2.h>
#include <asm/udbg.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/mpc8260.h>
#include <asm/prom.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/cpm2_pic.h>

#include "pq2.h"

static u8 __iomem *ep8248e_bcsr;
static struct device_node *ep8248e_bcsr_node;

#define BCSR7_SCC2_ENABLE 0x10

#define BCSR8_PHY1_ENABLE 0x80
#define BCSR8_PHY1_POWER  0x40
#define BCSR8_PHY2_ENABLE 0x20
#define BCSR8_PHY2_POWER  0x10
#define BCSR8_MDIO_READ   0x04
#define BCSR8_MDIO_CLOCK  0x02
#define BCSR8_MDIO_DATA   0x01

#define BCSR9_USB_ENABLE  0x80
#define BCSR9_USB_POWER   0x40
#define BCSR9_USB_HOST    0x20
#define BCSR9_USB_FULL_SPEED_TARGET 0x10

static void __init ep8248e_pic_init(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, "fsl,pq2-pic");
	if (!np) {
		printk(KERN_ERR "PIC init: can not find cpm-pic node\n");
		return;
	}

	cpm2_pic_init(np);
	of_node_put(np);
}

static void ep8248e_set_mdc(struct mdiobb_ctrl *ctrl, int level)
{
	if (level)
		setbits8(&ep8248e_bcsr[8], BCSR8_MDIO_CLOCK);
	else
		clrbits8(&ep8248e_bcsr[8], BCSR8_MDIO_CLOCK);

	/* Read back to flush the write. */
	in_8(&ep8248e_bcsr[8]);
}

static void ep8248e_set_mdio_dir(struct mdiobb_ctrl *ctrl, int output)
{
	if (output)
		clrbits8(&ep8248e_bcsr[8], BCSR8_MDIO_READ);
	else
		setbits8(&ep8248e_bcsr[8], BCSR8_MDIO_READ);

	/* Read back to flush the write. */
	in_8(&ep8248e_bcsr[8]);
}

static void ep8248e_set_mdio_data(struct mdiobb_ctrl *ctrl, int data)
{
	if (data)
		setbits8(&ep8248e_bcsr[8], BCSR8_MDIO_DATA);
	else
		clrbits8(&ep8248e_bcsr[8], BCSR8_MDIO_DATA);

	/* Read back to flush the write. */
	in_8(&ep8248e_bcsr[8]);
}

static int ep8248e_get_mdio_data(struct mdiobb_ctrl *ctrl)
{
	return in_8(&ep8248e_bcsr[8]) & BCSR8_MDIO_DATA;
}

static const struct mdiobb_ops ep8248e_mdio_ops = {
	.set_mdc = ep8248e_set_mdc,
	.set_mdio_dir = ep8248e_set_mdio_dir,
	.set_mdio_data = ep8248e_set_mdio_data,
	.get_mdio_data = ep8248e_get_mdio_data,
	.owner = THIS_MODULE,
};

static struct mdiobb_ctrl ep8248e_mdio_ctrl = {
	.ops = &ep8248e_mdio_ops,
};

static int __devinit ep8248e_mdio_probe(struct of_device *ofdev,
                                        const struct of_device_id *match)
{
	struct mii_bus *bus;
	struct resource res;
	struct device_node *node;
	int ret;

	node = of_get_parent(ofdev->node);
	of_node_put(node);
	if (node != ep8248e_bcsr_node)
		return -ENODEV;

	ret = of_address_to_resource(ofdev->node, 0, &res);
	if (ret)
		return ret;

	bus = alloc_mdio_bitbang(&ep8248e_mdio_ctrl);
	if (!bus)
		return -ENOMEM;

	bus->irq = kmalloc(sizeof(int) * PHY_MAX_ADDR, GFP_KERNEL);
	if (bus->irq == NULL) {
		ret = -ENOMEM;
		goto err_free_bus;
	}

	bus->name = "ep8248e-mdio-bitbang";
	bus->parent = &ofdev->dev;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%x", res.start);

	ret = of_mdiobus_register(bus, ofdev->node);
	if (ret)
		goto err_free_irq;

	return 0;
err_free_irq:
	kfree(bus->irq);
err_free_bus:
	free_mdio_bitbang(bus);
	return ret;
}

static int ep8248e_mdio_remove(struct of_device *ofdev)
{
	BUG();
	return 0;
}

static const struct of_device_id ep8248e_mdio_match[] = {
	{
		.compatible = "fsl,ep8248e-mdio-bitbang",
	},
	{},
};

static struct of_platform_driver ep8248e_mdio_driver = {
	.driver = {
		.name = "ep8248e-mdio-bitbang",
	},
	.match_table = ep8248e_mdio_match,
	.probe = ep8248e_mdio_probe,
	.remove = ep8248e_mdio_remove,
};

struct cpm_pin {
	int port, pin, flags;
};

static __initdata struct cpm_pin ep8248e_pins[] = {
	/* SMC1 */
	{2, 4, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 5, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},

	/* SCC1 */
	{2, 14, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 15, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{3, 29, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{3, 30, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{3, 31, CPM_PIN_INPUT | CPM_PIN_PRIMARY},

	/* FCC1 */
	{0, 14, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{0, 15, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{0, 16, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{0, 17, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{0, 18, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{0, 19, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{0, 20, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{0, 21, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{0, 26, CPM_PIN_INPUT | CPM_PIN_SECONDARY},
	{0, 27, CPM_PIN_INPUT | CPM_PIN_SECONDARY},
	{0, 28, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{0, 29, CPM_PIN_OUTPUT | CPM_PIN_SECONDARY},
	{0, 30, CPM_PIN_INPUT | CPM_PIN_SECONDARY},
	{0, 31, CPM_PIN_INPUT | CPM_PIN_SECONDARY},
	{2, 21, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 22, CPM_PIN_INPUT | CPM_PIN_PRIMARY},

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

	/* I2C */
	{4, 14, CPM_PIN_INPUT | CPM_PIN_SECONDARY},
	{4, 15, CPM_PIN_INPUT | CPM_PIN_SECONDARY},

	/* USB */
	{2, 10, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 11, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{2, 20, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{2, 24, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
	{3, 23, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{3, 24, CPM_PIN_OUTPUT | CPM_PIN_PRIMARY},
	{3, 25, CPM_PIN_INPUT | CPM_PIN_PRIMARY},
};

static void __init init_ioports(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ep8248e_pins); i++) {
		const struct cpm_pin *pin = &ep8248e_pins[i];
		cpm2_set_pin(pin->port, pin->pin, pin->flags);
	}

	cpm2_smc_clk_setup(CPM_CLK_SMC1, CPM_BRG7);
	cpm2_clk_setup(CPM_CLK_SCC1, CPM_BRG1, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_SCC1, CPM_BRG1, CPM_CLK_TX);
	cpm2_clk_setup(CPM_CLK_SCC3, CPM_CLK8, CPM_CLK_TX); /* USB */
	cpm2_clk_setup(CPM_CLK_FCC1, CPM_CLK11, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_FCC1, CPM_CLK10, CPM_CLK_TX);
	cpm2_clk_setup(CPM_CLK_FCC2, CPM_CLK13, CPM_CLK_RX);
	cpm2_clk_setup(CPM_CLK_FCC2, CPM_CLK14, CPM_CLK_TX);
}

static void __init ep8248e_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("ep8248e_setup_arch()", 0);

	cpm2_reset();

	/* When this is set, snooping CPM DMA from RAM causes
	 * machine checks.  See erratum SIU18.
	 */
	clrbits32(&cpm2_immr->im_siu_conf.siu_82xx.sc_bcr, MPC82XX_BCR_PLDP);

	ep8248e_bcsr_node =
		of_find_compatible_node(NULL, NULL, "fsl,ep8248e-bcsr");
	if (!ep8248e_bcsr_node) {
		printk(KERN_ERR "No bcsr in device tree\n");
		return;
	}

	ep8248e_bcsr = of_iomap(ep8248e_bcsr_node, 0);
	if (!ep8248e_bcsr) {
		printk(KERN_ERR "Cannot map BCSR registers\n");
		of_node_put(ep8248e_bcsr_node);
		ep8248e_bcsr_node = NULL;
		return;
	}

	setbits8(&ep8248e_bcsr[7], BCSR7_SCC2_ENABLE);
	setbits8(&ep8248e_bcsr[8], BCSR8_PHY1_ENABLE | BCSR8_PHY1_POWER |
	                           BCSR8_PHY2_ENABLE | BCSR8_PHY2_POWER);

	init_ioports();

	if (ppc_md.progress)
		ppc_md.progress("ep8248e_setup_arch(), finish", 0);
}

static  __initdata struct of_device_id of_bus_ids[] = {
	{ .compatible = "simple-bus", },
	{ .compatible = "fsl,ep8248e-bcsr", },
	{},
};

static int __init declare_of_platform_devices(void)
{
	of_platform_bus_probe(NULL, of_bus_ids, NULL);
	of_register_platform_driver(&ep8248e_mdio_driver);

	return 0;
}
machine_device_initcall(ep8248e, declare_of_platform_devices);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init ep8248e_probe(void)
{
	unsigned long root = of_get_flat_dt_root();
	return of_flat_dt_is_compatible(root, "fsl,ep8248e");
}

define_machine(ep8248e)
{
	.name = "Embedded Planet EP8248E",
	.probe = ep8248e_probe,
	.setup_arch = ep8248e_setup_arch,
	.init_IRQ = ep8248e_pic_init,
	.get_irq = cpm2_get_irq,
	.calibrate_decr = generic_calibrate_decr,
	.restart = pq2_restart,
	.progress = udbg_progress,
};
