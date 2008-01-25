/*
 *
 * Utility functions for the Freescale MPC52xx.
 *
 * Copyright (C) 2006 Sylvain Munaut <tnt@246tNt.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/mpc52xx.h>

/* MPC5200 device tree match tables */
static struct of_device_id mpc52xx_xlb_ids[] __initdata = {
	{ .compatible = "fsl,mpc5200-xlb", },
	{ .compatible = "mpc5200-xlb", },
	{}
};
static struct of_device_id mpc52xx_bus_ids[] __initdata = {
	{ .compatible = "fsl,mpc5200-immr", },
	{ .compatible = "fsl,mpc5200b-immr", },
	{ .compatible = "fsl,lpb", },

	/* depreciated matches; shouldn't be used in new device trees */
	{ .type = "builtin", .compatible = "mpc5200", }, /* efika */
	{ .type = "soc", .compatible = "mpc5200", }, /* lite5200 */
	{}
};

/*
 * This variable is mapped in mpc52xx_map_wdt() and used in mpc52xx_restart().
 * Permanent mapping is required because mpc52xx_restart() can be called
 * from interrupt context while node mapping (which calls ioremap())
 * cannot be used at such point.
 */
static volatile struct mpc52xx_gpt *mpc52xx_wdt = NULL;

/**
 * 	mpc52xx_find_ipb_freq - Find the IPB bus frequency for a device
 * 	@node:	device node
 *
 * 	Returns IPB bus frequency, or 0 if the bus frequency cannot be found.
 */
unsigned int
mpc52xx_find_ipb_freq(struct device_node *node)
{
	struct device_node *np;
	const unsigned int *p_ipb_freq = NULL;

	of_node_get(node);
	while (node) {
		p_ipb_freq = of_get_property(node, "bus-frequency", NULL);
		if (p_ipb_freq)
			break;

		np = of_get_parent(node);
		of_node_put(node);
		node = np;
	}
	if (node)
		of_node_put(node);

	return p_ipb_freq ? *p_ipb_freq : 0;
}
EXPORT_SYMBOL(mpc52xx_find_ipb_freq);


/*
 * Configure the XLB arbiter settings to match what Linux expects.
 */
void __init
mpc5200_setup_xlb_arbiter(void)
{
	struct device_node *np;
	struct mpc52xx_xlb  __iomem *xlb;

	np = of_find_matching_node(NULL, mpc52xx_xlb_ids);
	xlb = of_iomap(np, 0);
	of_node_put(np);
	if (!xlb) {
		printk(KERN_ERR __FILE__ ": "
			"Error mapping XLB in mpc52xx_setup_cpu().  "
			"Expect some abnormal behavior\n");
		return;
	}

	/* Configure the XLB Arbiter priorities */
	out_be32(&xlb->master_pri_enable, 0xff);
	out_be32(&xlb->master_priority, 0x11111111);

	/* Disable XLB pipelining
	 * (cfr errate 292. We could do this only just before ATA PIO
	 *  transaction and re-enable it afterwards ...)
	 */
	out_be32(&xlb->config, in_be32(&xlb->config) | MPC52xx_XLB_CFG_PLDIS);

	iounmap(xlb);
}

/**
 * mpc52xx_declare_of_platform_devices: register internal devices and children
 *					of the localplus bus to the of_platform
 *					bus.
 */
void __init
mpc52xx_declare_of_platform_devices(void)
{
	/* Find every child of the SOC node and add it to of_platform */
	if (of_platform_bus_probe(NULL, mpc52xx_bus_ids, NULL))
		printk(KERN_ERR __FILE__ ": "
			"Error while probing of_platform bus\n");
}

/*
 * match tables used by mpc52xx_map_wdt()
 */
static struct of_device_id mpc52xx_gpt_ids[] __initdata = {
	{ .compatible = "fsl,mpc5200-gpt", },
	{ .compatible = "mpc5200-gpt", }, /* old */
	{}
};

void __init
mpc52xx_map_wdt(void)
{
	struct device_node *np;
	/* mpc52xx_wdt is mapped here and used in mpc52xx_restart,
	 * possibly from a interrupt context. wdt is only implement
	 * on a gpt0, so check has-wdt property before mapping.
	 */
	for_each_matching_node(np, mpc52xx_gpt_ids) {
		if (of_get_property(np, "fsl,has-wdt", NULL) ||
		    of_get_property(np, "has-wdt", NULL)) {
			mpc52xx_wdt = of_iomap(np, 0);
			of_node_put(np);
			return;
		}
	}
}

void
mpc52xx_restart(char *cmd)
{
	local_irq_disable();

	/* Turn on the watchdog and wait for it to expire.
	 * It effectively does a reset. */
	if (mpc52xx_wdt) {
		out_be32(&mpc52xx_wdt->mode, 0x00000000);
		out_be32(&mpc52xx_wdt->count, 0x000000ff);
		out_be32(&mpc52xx_wdt->mode, 0x00009004);
	} else
		printk("mpc52xx_restart: Can't access wdt. "
			"Restart impossible, system halted.\n");

	while (1);
}
