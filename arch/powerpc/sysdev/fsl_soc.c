/*
 * FSL SoC setup code
 *
 * Maintained by Kumar Gala (see MAINTAINERS for contact information)
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/fsl_devices.h>

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <sysdev/fsl_soc.h>
#include <mm/mmu_decl.h>

static phys_addr_t immrbase = -1;

phys_addr_t get_immrbase(void)
{
	struct device_node *soc;

	if (immrbase != -1)
		return immrbase;

	soc = of_find_node_by_type(NULL, "soc");
	if (soc != 0) {
		unsigned int size;
		void *prop = get_property(soc, "reg", &size);
		immrbase = of_translate_address(soc, prop);
		of_node_put(soc);
	};

	return immrbase;
}
EXPORT_SYMBOL(get_immrbase);

static const char * gfar_tx_intr = "tx";
static const char * gfar_rx_intr = "rx";
static const char * gfar_err_intr = "error";

static int __init gfar_of_init(void)
{
	struct device_node *np;
	unsigned int i;
	struct platform_device *mdio_dev, *gfar_dev;
	struct resource res;
	int ret;

	for (np = NULL, i = 0; (np = of_find_compatible_node(np, "mdio", "gianfar")) != NULL; i++) {
		int k;
		struct device_node *child = NULL;
		struct gianfar_mdio_data mdio_data;

		memset(&res, 0, sizeof(res));
		memset(&mdio_data, 0, sizeof(mdio_data));

		ret = of_address_to_resource(np, 0, &res);
		if (ret)
			goto mdio_err;

		mdio_dev = platform_device_register_simple("fsl-gianfar_mdio", res.start, &res, 1);
		if (IS_ERR(mdio_dev)) {
			ret = PTR_ERR(mdio_dev);
			goto mdio_err;
		}

		for (k = 0; k < 32; k++)
			mdio_data.irq[k] = -1;

		while ((child = of_get_next_child(np, child)) != NULL) {
			if (child->n_intrs) {
				u32 *id = (u32 *) get_property(child, "reg", NULL);
				mdio_data.irq[*id] = child->intrs[0].line;
			}
		}

		ret = platform_device_add_data(mdio_dev, &mdio_data, sizeof(struct gianfar_mdio_data));
		if (ret)
			goto mdio_unreg;
	}

	for (np = NULL, i = 0; (np = of_find_compatible_node(np, "network", "gianfar")) != NULL; i++) {
		struct resource r[4];
		struct device_node *phy, *mdio;
		struct gianfar_platform_data gfar_data;
		unsigned int *id;
		char *model;
		void *mac_addr;
		phandle *ph;

		memset(r, 0, sizeof(r));
		memset(&gfar_data, 0, sizeof(gfar_data));

		ret = of_address_to_resource(np, 0, &r[0]);
		if (ret)
			goto gfar_err;

		r[1].start = np->intrs[0].line;
		r[1].end = np->intrs[0].line;
		r[1].flags = IORESOURCE_IRQ;

		model = get_property(np, "model", NULL);

		/* If we aren't the FEC we have multiple interrupts */
		if (model && strcasecmp(model, "FEC")) {
			r[1].name = gfar_tx_intr;

			r[2].name = gfar_rx_intr;
			r[2].start = np->intrs[1].line;
			r[2].end = np->intrs[1].line;
			r[2].flags = IORESOURCE_IRQ;

			r[3].name = gfar_err_intr;
			r[3].start = np->intrs[2].line;
			r[3].end = np->intrs[2].line;
			r[3].flags = IORESOURCE_IRQ;
		}

		gfar_dev = platform_device_register_simple("fsl-gianfar", i, &r[0], np->n_intrs + 1);

		if (IS_ERR(gfar_dev)) {
			ret = PTR_ERR(gfar_dev);
			goto gfar_err;
		}

		mac_addr = get_property(np, "address", NULL);
		memcpy(gfar_data.mac_addr, mac_addr, 6);

		if (model && !strcasecmp(model, "TSEC"))
			gfar_data.device_flags =
				FSL_GIANFAR_DEV_HAS_GIGABIT |
				FSL_GIANFAR_DEV_HAS_COALESCE |
				FSL_GIANFAR_DEV_HAS_RMON |
				FSL_GIANFAR_DEV_HAS_MULTI_INTR;
		if (model && !strcasecmp(model, "eTSEC"))
			gfar_data.device_flags =
				FSL_GIANFAR_DEV_HAS_GIGABIT |
				FSL_GIANFAR_DEV_HAS_COALESCE |
				FSL_GIANFAR_DEV_HAS_RMON |
				FSL_GIANFAR_DEV_HAS_MULTI_INTR |
				FSL_GIANFAR_DEV_HAS_CSUM |
				FSL_GIANFAR_DEV_HAS_VLAN |
				FSL_GIANFAR_DEV_HAS_EXTENDED_HASH;

		ph = (phandle *) get_property(np, "phy-handle", NULL);
		phy = of_find_node_by_phandle(*ph);

		if (phy == NULL) {
			ret = -ENODEV;
			goto gfar_unreg;
		}

		mdio = of_get_parent(phy);

		id = (u32 *) get_property(phy, "reg", NULL);
		ret = of_address_to_resource(mdio, 0, &res);
		if (ret) {
			of_node_put(phy);
			of_node_put(mdio);
			goto gfar_unreg;
		}

		gfar_data.phy_id = *id;
		gfar_data.bus_id = res.start;

		of_node_put(phy);
		of_node_put(mdio);

		ret = platform_device_add_data(gfar_dev, &gfar_data, sizeof(struct gianfar_platform_data));
		if (ret)
			goto gfar_unreg;
	}

	return 0;

mdio_unreg:
	platform_device_unregister(mdio_dev);
mdio_err:
	return ret;

gfar_unreg:
	platform_device_unregister(gfar_dev);
gfar_err:
	return ret;
}
arch_initcall(gfar_of_init);

static int __init fsl_i2c_of_init(void)
{
	struct device_node *np;
	unsigned int i;
	struct platform_device *i2c_dev;
	int ret;

	for (np = NULL, i = 0; (np = of_find_compatible_node(np, "i2c", "fsl-i2c")) != NULL; i++) {
		struct resource r[2];
		struct fsl_i2c_platform_data i2c_data;
		unsigned char * flags = NULL;

		memset(&r, 0, sizeof(r));
		memset(&i2c_data, 0, sizeof(i2c_data));

		ret = of_address_to_resource(np, 0, &r[0]);
		if (ret)
			goto i2c_err;

		r[1].start = np->intrs[0].line;
		r[1].end = np->intrs[0].line;
		r[1].flags = IORESOURCE_IRQ;

		i2c_dev = platform_device_register_simple("fsl-i2c", i, r, 2);
		if (IS_ERR(i2c_dev)) {
			ret = PTR_ERR(i2c_dev);
			goto i2c_err;
		}

		i2c_data.device_flags = 0;
		flags = get_property(np, "dfsrr", NULL);
		if (flags)
			i2c_data.device_flags |= FSL_I2C_DEV_SEPARATE_DFSRR;

		flags = get_property(np, "fsl5200-clocking", NULL);
		if (flags)
			i2c_data.device_flags |= FSL_I2C_DEV_CLOCK_5200;

		ret = platform_device_add_data(i2c_dev, &i2c_data, sizeof(struct fsl_i2c_platform_data));
		if (ret)
			goto i2c_unreg;
	}

	return 0;

i2c_unreg:
	platform_device_unregister(i2c_dev);
i2c_err:
	return ret;
}
arch_initcall(fsl_i2c_of_init);

#ifdef CONFIG_PPC_83xx
static int __init mpc83xx_wdt_init(void)
{
	struct resource r;
	struct device_node *soc, *np;
	struct platform_device *dev;
	unsigned int *freq;
	int ret;

	np = of_find_compatible_node(NULL, "watchdog", "mpc83xx_wdt");

	if (!np) {
		ret = -ENODEV;
		goto mpc83xx_wdt_nodev;
	}

	soc = of_find_node_by_type(NULL, "soc");

	if (!soc) {
		ret = -ENODEV;
		goto mpc83xx_wdt_nosoc;
	}

	freq = (unsigned int *)get_property(soc, "bus-frequency", NULL);
	if (!freq) {
		ret = -ENODEV;
		goto mpc83xx_wdt_err;
	}

	memset(&r, 0, sizeof(r));

	ret = of_address_to_resource(np, 0, &r);
	if (ret)
		goto mpc83xx_wdt_err;

	dev = platform_device_register_simple("mpc83xx_wdt", 0, &r, 1);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto mpc83xx_wdt_err;
	}

	ret = platform_device_add_data(dev, freq, sizeof(int));
	if (ret)
		goto mpc83xx_wdt_unreg;

	of_node_put(soc);
	of_node_put(np);

	return 0;

mpc83xx_wdt_unreg:
	platform_device_unregister(dev);
mpc83xx_wdt_err:
	of_node_put(soc);
mpc83xx_wdt_nosoc:
	of_node_put(np);
mpc83xx_wdt_nodev:
	return ret;
}
arch_initcall(mpc83xx_wdt_init);
#endif
