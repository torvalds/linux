/*
 * FSL SoC setup code
 *
 * Maintained by Kumar Gala (see MAINTAINERS for contact information)
 *
 * 2006 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

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
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/spi/spi.h>
#include <linux/fsl_devices.h>
#include <linux/fs_enet_pd.h>
#include <linux/fs_uart_pd.h>

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/time.h>
#include <asm/prom.h>
#include <sysdev/fsl_soc.h>
#include <mm/mmu_decl.h>
#include <asm/cpm2.h>

extern void init_fcc_ioports(struct fs_platform_info*);
extern void init_fec_ioports(struct fs_platform_info*);
extern void init_smc_ioports(struct fs_uart_platform_info*);
static phys_addr_t immrbase = -1;

phys_addr_t get_immrbase(void)
{
	struct device_node *soc;

	if (immrbase != -1)
		return immrbase;

	soc = of_find_node_by_type(NULL, "soc");
	if (soc) {
		int size;
		u32 naddr;
		const u32 *prop = of_get_property(soc, "#address-cells", &size);

		if (prop && size == 4)
			naddr = *prop;
		else
			naddr = 2;

		prop = of_get_property(soc, "ranges", &size);
		if (prop)
			immrbase = of_translate_address(soc, prop + naddr);

		of_node_put(soc);
	}

	return immrbase;
}

EXPORT_SYMBOL(get_immrbase);

static u32 sysfreq = -1;

u32 fsl_get_sys_freq(void)
{
	struct device_node *soc;
	const u32 *prop;
	int size;

	if (sysfreq != -1)
		return sysfreq;

	soc = of_find_node_by_type(NULL, "soc");
	if (!soc)
		return -1;

	prop = of_get_property(soc, "clock-frequency", &size);
	if (!prop || size != sizeof(*prop) || *prop == 0)
		prop = of_get_property(soc, "bus-frequency", &size);

	if (prop && size == sizeof(*prop))
		sysfreq = *prop;

	of_node_put(soc);
	return sysfreq;
}
EXPORT_SYMBOL(fsl_get_sys_freq);

#if defined(CONFIG_CPM2) || defined(CONFIG_QUICC_ENGINE) || defined(CONFIG_8xx)

static u32 brgfreq = -1;

u32 get_brgfreq(void)
{
	struct device_node *node;
	const unsigned int *prop;
	int size;

	if (brgfreq != -1)
		return brgfreq;

	node = of_find_compatible_node(NULL, NULL, "fsl,cpm-brg");
	if (node) {
		prop = of_get_property(node, "clock-frequency", &size);
		if (prop && size == 4)
			brgfreq = *prop;

		of_node_put(node);
		return brgfreq;
	}

	/* Legacy device binding -- will go away when no users are left. */
	node = of_find_node_by_type(NULL, "cpm");
	if (!node)
		node = of_find_compatible_node(NULL, NULL, "fsl,qe");
	if (!node)
		node = of_find_node_by_type(NULL, "qe");

	if (node) {
		prop = of_get_property(node, "brg-frequency", &size);
		if (prop && size == 4)
			brgfreq = *prop;

		if (brgfreq == -1 || brgfreq == 0) {
			prop = of_get_property(node, "bus-frequency", &size);
			if (prop && size == 4)
				brgfreq = *prop / 2;
		}
		of_node_put(node);
	}

	return brgfreq;
}

EXPORT_SYMBOL(get_brgfreq);

static u32 fs_baudrate = -1;

u32 get_baudrate(void)
{
	struct device_node *node;

	if (fs_baudrate != -1)
		return fs_baudrate;

	node = of_find_node_by_type(NULL, "serial");
	if (node) {
		int size;
		const unsigned int *prop = of_get_property(node,
				"current-speed", &size);

		if (prop)
			fs_baudrate = *prop;
		of_node_put(node);
	}

	return fs_baudrate;
}

EXPORT_SYMBOL(get_baudrate);
#endif /* CONFIG_CPM2 */

#ifdef CONFIG_FIXED_PHY
static int __init of_add_fixed_phys(void)
{
	int ret;
	struct device_node *np;
	u32 *fixed_link;
	struct fixed_phy_status status = {};

	for_each_node_by_name(np, "ethernet") {
		fixed_link  = (u32 *)of_get_property(np, "fixed-link", NULL);
		if (!fixed_link)
			continue;

		status.link = 1;
		status.duplex = fixed_link[1];
		status.speed = fixed_link[2];
		status.pause = fixed_link[3];
		status.asym_pause = fixed_link[4];

		ret = fixed_phy_add(PHY_POLL, fixed_link[0], &status);
		if (ret) {
			of_node_put(np);
			return ret;
		}
	}

	return 0;
}
arch_initcall(of_add_fixed_phys);
#endif /* CONFIG_FIXED_PHY */

static int gfar_mdio_of_init_one(struct device_node *np)
{
	int k;
	struct device_node *child = NULL;
	struct gianfar_mdio_data mdio_data;
	struct platform_device *mdio_dev;
	struct resource res;
	int ret;

	memset(&res, 0, sizeof(res));
	memset(&mdio_data, 0, sizeof(mdio_data));

	ret = of_address_to_resource(np, 0, &res);
	if (ret)
		return ret;

	mdio_dev = platform_device_register_simple("fsl-gianfar_mdio",
			res.start&0xfffff, &res, 1);
	if (IS_ERR(mdio_dev))
		return PTR_ERR(mdio_dev);

	for (k = 0; k < 32; k++)
		mdio_data.irq[k] = PHY_POLL;

	while ((child = of_get_next_child(np, child)) != NULL) {
		int irq = irq_of_parse_and_map(child, 0);
		if (irq != NO_IRQ) {
			const u32 *id = of_get_property(child, "reg", NULL);
			mdio_data.irq[*id] = irq;
		}
	}

	ret = platform_device_add_data(mdio_dev, &mdio_data,
				sizeof(struct gianfar_mdio_data));
	if (ret)
		platform_device_unregister(mdio_dev);

	return ret;
}

static int __init gfar_mdio_of_init(void)
{
	struct device_node *np = NULL;

	for_each_compatible_node(np, NULL, "fsl,gianfar-mdio")
		gfar_mdio_of_init_one(np);

	/* try the deprecated version */
	for_each_compatible_node(np, "mdio", "gianfar");
		gfar_mdio_of_init_one(np);

	return 0;
}

arch_initcall(gfar_mdio_of_init);

static const char *gfar_tx_intr = "tx";
static const char *gfar_rx_intr = "rx";
static const char *gfar_err_intr = "error";

static int __init gfar_of_init(void)
{
	struct device_node *np;
	unsigned int i;
	struct platform_device *gfar_dev;
	struct resource res;
	int ret;

	for (np = NULL, i = 0;
	     (np = of_find_compatible_node(np, "network", "gianfar")) != NULL;
	     i++) {
		struct resource r[4];
		struct device_node *phy, *mdio;
		struct gianfar_platform_data gfar_data;
		const unsigned int *id;
		const char *model;
		const char *ctype;
		const void *mac_addr;
		const phandle *ph;
		int n_res = 2;

		if (!of_device_is_available(np))
			continue;

		memset(r, 0, sizeof(r));
		memset(&gfar_data, 0, sizeof(gfar_data));

		ret = of_address_to_resource(np, 0, &r[0]);
		if (ret)
			goto err;

		of_irq_to_resource(np, 0, &r[1]);

		model = of_get_property(np, "model", NULL);

		/* If we aren't the FEC we have multiple interrupts */
		if (model && strcasecmp(model, "FEC")) {
			r[1].name = gfar_tx_intr;

			r[2].name = gfar_rx_intr;
			of_irq_to_resource(np, 1, &r[2]);

			r[3].name = gfar_err_intr;
			of_irq_to_resource(np, 2, &r[3]);

			n_res += 2;
		}

		gfar_dev =
		    platform_device_register_simple("fsl-gianfar", i, &r[0],
						    n_res);

		if (IS_ERR(gfar_dev)) {
			ret = PTR_ERR(gfar_dev);
			goto err;
		}

		mac_addr = of_get_mac_address(np);
		if (mac_addr)
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

		ctype = of_get_property(np, "phy-connection-type", NULL);

		/* We only care about rgmii-id.  The rest are autodetected */
		if (ctype && !strcmp(ctype, "rgmii-id"))
			gfar_data.interface = PHY_INTERFACE_MODE_RGMII_ID;
		else
			gfar_data.interface = PHY_INTERFACE_MODE_MII;

		if (of_get_property(np, "fsl,magic-packet", NULL))
			gfar_data.device_flags |= FSL_GIANFAR_DEV_HAS_MAGIC_PACKET;

		ph = of_get_property(np, "phy-handle", NULL);
		if (ph == NULL) {
			u32 *fixed_link;

			fixed_link = (u32 *)of_get_property(np, "fixed-link",
							   NULL);
			if (!fixed_link) {
				ret = -ENODEV;
				goto unreg;
			}

			snprintf(gfar_data.bus_id, MII_BUS_ID_SIZE, "0");
			gfar_data.phy_id = fixed_link[0];
		} else {
			phy = of_find_node_by_phandle(*ph);

			if (phy == NULL) {
				ret = -ENODEV;
				goto unreg;
			}

			mdio = of_get_parent(phy);

			id = of_get_property(phy, "reg", NULL);
			ret = of_address_to_resource(mdio, 0, &res);
			if (ret) {
				of_node_put(phy);
				of_node_put(mdio);
				goto unreg;
			}

			gfar_data.phy_id = *id;
			snprintf(gfar_data.bus_id, MII_BUS_ID_SIZE, "%llx",
				 (unsigned long long)res.start&0xfffff);

			of_node_put(phy);
			of_node_put(mdio);
		}

		ret =
		    platform_device_add_data(gfar_dev, &gfar_data,
					     sizeof(struct
						    gianfar_platform_data));
		if (ret)
			goto unreg;
	}

	return 0;

unreg:
	platform_device_unregister(gfar_dev);
err:
	return ret;
}

arch_initcall(gfar_of_init);


#ifdef CONFIG_PPC_83xx
static int __init mpc83xx_wdt_init(void)
{
	struct resource r;
	struct device_node *np;
	struct platform_device *dev;
	u32 freq = fsl_get_sys_freq();
	int ret;

	np = of_find_compatible_node(NULL, "watchdog", "mpc83xx_wdt");

	if (!np) {
		ret = -ENODEV;
		goto nodev;
	}

	memset(&r, 0, sizeof(r));

	ret = of_address_to_resource(np, 0, &r);
	if (ret)
		goto err;

	dev = platform_device_register_simple("mpc83xx_wdt", 0, &r, 1);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err;
	}

	ret = platform_device_add_data(dev, &freq, sizeof(freq));
	if (ret)
		goto unreg;

	of_node_put(np);
	return 0;

unreg:
	platform_device_unregister(dev);
err:
	of_node_put(np);
nodev:
	return ret;
}

arch_initcall(mpc83xx_wdt_init);
#endif

static enum fsl_usb2_phy_modes determine_usb_phy(const char *phy_type)
{
	if (!phy_type)
		return FSL_USB2_PHY_NONE;
	if (!strcasecmp(phy_type, "ulpi"))
		return FSL_USB2_PHY_ULPI;
	if (!strcasecmp(phy_type, "utmi"))
		return FSL_USB2_PHY_UTMI;
	if (!strcasecmp(phy_type, "utmi_wide"))
		return FSL_USB2_PHY_UTMI_WIDE;
	if (!strcasecmp(phy_type, "serial"))
		return FSL_USB2_PHY_SERIAL;

	return FSL_USB2_PHY_NONE;
}

static int __init fsl_usb_of_init(void)
{
	struct device_node *np;
	unsigned int i = 0;
	struct platform_device *usb_dev_mph = NULL, *usb_dev_dr_host = NULL,
		*usb_dev_dr_client = NULL;
	int ret;

	for_each_compatible_node(np, NULL, "fsl-usb2-mph") {
		struct resource r[2];
		struct fsl_usb2_platform_data usb_data;
		const unsigned char *prop = NULL;

		memset(&r, 0, sizeof(r));
		memset(&usb_data, 0, sizeof(usb_data));

		ret = of_address_to_resource(np, 0, &r[0]);
		if (ret)
			goto err;

		of_irq_to_resource(np, 0, &r[1]);

		usb_dev_mph =
		    platform_device_register_simple("fsl-ehci", i, r, 2);
		if (IS_ERR(usb_dev_mph)) {
			ret = PTR_ERR(usb_dev_mph);
			goto err;
		}

		usb_dev_mph->dev.coherent_dma_mask = 0xffffffffUL;
		usb_dev_mph->dev.dma_mask = &usb_dev_mph->dev.coherent_dma_mask;

		usb_data.operating_mode = FSL_USB2_MPH_HOST;

		prop = of_get_property(np, "port0", NULL);
		if (prop)
			usb_data.port_enables |= FSL_USB2_PORT0_ENABLED;

		prop = of_get_property(np, "port1", NULL);
		if (prop)
			usb_data.port_enables |= FSL_USB2_PORT1_ENABLED;

		prop = of_get_property(np, "phy_type", NULL);
		usb_data.phy_mode = determine_usb_phy(prop);

		ret =
		    platform_device_add_data(usb_dev_mph, &usb_data,
					     sizeof(struct
						    fsl_usb2_platform_data));
		if (ret)
			goto unreg_mph;
		i++;
	}

	for_each_compatible_node(np, NULL, "fsl-usb2-dr") {
		struct resource r[2];
		struct fsl_usb2_platform_data usb_data;
		const unsigned char *prop = NULL;

		memset(&r, 0, sizeof(r));
		memset(&usb_data, 0, sizeof(usb_data));

		ret = of_address_to_resource(np, 0, &r[0]);
		if (ret)
			goto unreg_mph;

		of_irq_to_resource(np, 0, &r[1]);

		prop = of_get_property(np, "dr_mode", NULL);

		if (!prop || !strcmp(prop, "host")) {
			usb_data.operating_mode = FSL_USB2_DR_HOST;
			usb_dev_dr_host = platform_device_register_simple(
					"fsl-ehci", i, r, 2);
			if (IS_ERR(usb_dev_dr_host)) {
				ret = PTR_ERR(usb_dev_dr_host);
				goto err;
			}
		} else if (prop && !strcmp(prop, "peripheral")) {
			usb_data.operating_mode = FSL_USB2_DR_DEVICE;
			usb_dev_dr_client = platform_device_register_simple(
					"fsl-usb2-udc", i, r, 2);
			if (IS_ERR(usb_dev_dr_client)) {
				ret = PTR_ERR(usb_dev_dr_client);
				goto err;
			}
		} else if (prop && !strcmp(prop, "otg")) {
			usb_data.operating_mode = FSL_USB2_DR_OTG;
			usb_dev_dr_host = platform_device_register_simple(
					"fsl-ehci", i, r, 2);
			if (IS_ERR(usb_dev_dr_host)) {
				ret = PTR_ERR(usb_dev_dr_host);
				goto err;
			}
			usb_dev_dr_client = platform_device_register_simple(
					"fsl-usb2-udc", i, r, 2);
			if (IS_ERR(usb_dev_dr_client)) {
				ret = PTR_ERR(usb_dev_dr_client);
				goto err;
			}
		} else {
			ret = -EINVAL;
			goto err;
		}

		prop = of_get_property(np, "phy_type", NULL);
		usb_data.phy_mode = determine_usb_phy(prop);

		if (usb_dev_dr_host) {
			usb_dev_dr_host->dev.coherent_dma_mask = 0xffffffffUL;
			usb_dev_dr_host->dev.dma_mask = &usb_dev_dr_host->
				dev.coherent_dma_mask;
			if ((ret = platform_device_add_data(usb_dev_dr_host,
						&usb_data, sizeof(struct
						fsl_usb2_platform_data))))
				goto unreg_dr;
		}
		if (usb_dev_dr_client) {
			usb_dev_dr_client->dev.coherent_dma_mask = 0xffffffffUL;
			usb_dev_dr_client->dev.dma_mask = &usb_dev_dr_client->
				dev.coherent_dma_mask;
			if ((ret = platform_device_add_data(usb_dev_dr_client,
						&usb_data, sizeof(struct
						fsl_usb2_platform_data))))
				goto unreg_dr;
		}
		i++;
	}
	return 0;

unreg_dr:
	if (usb_dev_dr_host)
		platform_device_unregister(usb_dev_dr_host);
	if (usb_dev_dr_client)
		platform_device_unregister(usb_dev_dr_client);
unreg_mph:
	if (usb_dev_mph)
		platform_device_unregister(usb_dev_mph);
err:
	return ret;
}

arch_initcall(fsl_usb_of_init);

static int __init of_fsl_spi_probe(char *type, char *compatible, u32 sysclk,
				   struct spi_board_info *board_infos,
				   unsigned int num_board_infos,
				   void (*activate_cs)(u8 cs, u8 polarity),
				   void (*deactivate_cs)(u8 cs, u8 polarity))
{
	struct device_node *np;
	unsigned int i = 0;

	for_each_compatible_node(np, type, compatible) {
		int ret;
		unsigned int j;
		const void *prop;
		struct resource res[2];
		struct platform_device *pdev;
		struct fsl_spi_platform_data pdata = {
			.activate_cs = activate_cs,
			.deactivate_cs = deactivate_cs,
		};

		memset(res, 0, sizeof(res));

		pdata.sysclk = sysclk;

		prop = of_get_property(np, "reg", NULL);
		if (!prop)
			goto err;
		pdata.bus_num = *(u32 *)prop;

		prop = of_get_property(np, "cell-index", NULL);
		if (prop)
			i = *(u32 *)prop;

		prop = of_get_property(np, "mode", NULL);
		if (prop && !strcmp(prop, "cpu-qe"))
			pdata.qe_mode = 1;

		for (j = 0; j < num_board_infos; j++) {
			if (board_infos[j].bus_num == pdata.bus_num)
				pdata.max_chipselect++;
		}

		if (!pdata.max_chipselect)
			continue;

		ret = of_address_to_resource(np, 0, &res[0]);
		if (ret)
			goto err;

		ret = of_irq_to_resource(np, 0, &res[1]);
		if (ret == NO_IRQ)
			goto err;

		pdev = platform_device_alloc("mpc83xx_spi", i);
		if (!pdev)
			goto err;

		ret = platform_device_add_data(pdev, &pdata, sizeof(pdata));
		if (ret)
			goto unreg;

		ret = platform_device_add_resources(pdev, res,
						    ARRAY_SIZE(res));
		if (ret)
			goto unreg;

		ret = platform_device_add(pdev);
		if (ret)
			goto unreg;

		goto next;
unreg:
		platform_device_del(pdev);
err:
		pr_err("%s: registration failed\n", np->full_name);
next:
		i++;
	}

	return i;
}

int __init fsl_spi_init(struct spi_board_info *board_infos,
			unsigned int num_board_infos,
			void (*activate_cs)(u8 cs, u8 polarity),
			void (*deactivate_cs)(u8 cs, u8 polarity))
{
	u32 sysclk = -1;
	int ret;

#ifdef CONFIG_QUICC_ENGINE
	/* SPI controller is either clocked from QE or SoC clock */
	sysclk = get_brgfreq();
#endif
	if (sysclk == -1) {
		sysclk = fsl_get_sys_freq();
		if (sysclk == -1)
			return -ENODEV;
	}

	ret = of_fsl_spi_probe(NULL, "fsl,spi", sysclk, board_infos,
			       num_board_infos, activate_cs, deactivate_cs);
	if (!ret)
		of_fsl_spi_probe("spi", "fsl_spi", sysclk, board_infos,
				 num_board_infos, activate_cs, deactivate_cs);

	return spi_register_board_info(board_infos, num_board_infos);
}

#if defined(CONFIG_PPC_85xx) || defined(CONFIG_PPC_86xx)
static __be32 __iomem *rstcr;

static int __init setup_rstcr(void)
{
	struct device_node *np;
	np = of_find_node_by_name(NULL, "global-utilities");
	if ((np && of_get_property(np, "fsl,has-rstcr", NULL))) {
		const u32 *prop = of_get_property(np, "reg", NULL);
		if (prop) {
			/* map reset control register
			 * 0xE00B0 is offset of reset control register
			 */
			rstcr = ioremap(get_immrbase() + *prop + 0xB0, 0xff);
			if (!rstcr)
				printk (KERN_EMERG "Error: reset control "
						"register not mapped!\n");
		}
	} else
		printk (KERN_INFO "rstcr compatible register does not exist!\n");
	if (np)
		of_node_put(np);
	return 0;
}

arch_initcall(setup_rstcr);

void fsl_rstcr_restart(char *cmd)
{
	local_irq_disable();
	if (rstcr)
		/* set reset control register */
		out_be32(rstcr, 0x2);	/* HRESET_REQ */

	while (1) ;
}
#endif

#if defined(CONFIG_FB_FSL_DIU) || defined(CONFIG_FB_FSL_DIU_MODULE)
struct platform_diu_data_ops diu_ops = {
	.diu_size = 1280 * 1024 * 4,	/* default one 1280x1024 buffer */
};
EXPORT_SYMBOL(diu_ops);

int __init preallocate_diu_videomemory(void)
{
	pr_debug("diu_size=%lu\n", diu_ops.diu_size);

	diu_ops.diu_mem = __alloc_bootmem(diu_ops.diu_size, 8, 0);
	if (!diu_ops.diu_mem) {
		printk(KERN_ERR "fsl-diu: cannot allocate %lu bytes\n",
			diu_ops.diu_size);
		return -ENOMEM;
	}

	pr_debug("diu_mem=%p\n", diu_ops.diu_mem);

	rh_init(&diu_ops.diu_rh_info, 4096, ARRAY_SIZE(diu_ops.diu_rh_block),
		diu_ops.diu_rh_block);
	return rh_attach_region(&diu_ops.diu_rh_info,
				(unsigned long) diu_ops.diu_mem,
				diu_ops.diu_size);
}

static int __init early_parse_diufb(char *p)
{
	if (!p)
		return 1;

	diu_ops.diu_size = _ALIGN_UP(memparse(p, &p), 8);

	pr_debug("diu_size=%lu\n", diu_ops.diu_size);

	return 0;
}
early_param("diufb", early_parse_diufb);

#endif
