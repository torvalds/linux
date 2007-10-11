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


void __iomem *
mpc52xx_find_and_map(const char *compatible)
{
	struct device_node *ofn;
	const u32 *regaddr_p;
	u64 regaddr64, size64;

	ofn = of_find_compatible_node(NULL, NULL, compatible);
	if (!ofn)
		return NULL;

	regaddr_p = of_get_address(ofn, 0, &size64, NULL);
	if (!regaddr_p) {
		of_node_put(ofn);
		return NULL;
	}

	regaddr64 = of_translate_address(ofn, regaddr_p);

	of_node_put(ofn);

	return ioremap((u32)regaddr64, (u32)size64);
}
EXPORT_SYMBOL(mpc52xx_find_and_map);


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
	struct mpc52xx_xlb  __iomem *xlb;

	xlb = mpc52xx_find_and_map("mpc5200-xlb");
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

void __init
mpc52xx_declare_of_platform_devices(void)
{
	/* Find every child of the SOC node and add it to of_platform */
	if (of_platform_bus_probe(NULL, NULL, NULL))
		printk(KERN_ERR __FILE__ ": "
			"Error while probing of_platform bus\n");
}

