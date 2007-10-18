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

/*
 * This variable is mapped in mpc52xx_map_wdt() and used in mpc52xx_restart().
 * Permanent mapping is required because mpc52xx_restart() can be called
 * from interrupt context while node mapping (which calls ioremap())
 * cannot be used at such point.
 */
static volatile struct mpc52xx_gpt *mpc52xx_wdt = NULL;

static void __iomem *
mpc52xx_map_node(struct device_node *ofn)
{
	const u32 *regaddr_p;
	u64 regaddr64, size64;

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

void __iomem *
mpc52xx_find_and_map(const char *compatible)
{
	return mpc52xx_map_node(
		of_find_compatible_node(NULL, NULL, compatible));
}

EXPORT_SYMBOL(mpc52xx_find_and_map);

void __iomem *
mpc52xx_find_and_map_path(const char *path)
{
	return mpc52xx_map_node(of_find_node_by_path(path));
}

EXPORT_SYMBOL(mpc52xx_find_and_map_path);

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

void __init
mpc52xx_map_wdt(void)
{
	const void *has_wdt;
	struct device_node *np;

	/* mpc52xx_wdt is mapped here and used in mpc52xx_restart,
	 * possibly from a interrupt context. wdt is only implement
	 * on a gpt0, so check has-wdt property before mapping.
	 */
	for_each_compatible_node(np, NULL, "fsl,mpc5200-gpt") {
		has_wdt = of_get_property(np, "fsl,has-wdt", NULL);
		if (has_wdt) {
			mpc52xx_wdt = mpc52xx_map_node(np);
			return;
		}
	}
	for_each_compatible_node(np, NULL, "mpc5200-gpt") {
		has_wdt = of_get_property(np, "has-wdt", NULL);
		if (has_wdt) {
			mpc52xx_wdt = mpc52xx_map_node(np);
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
