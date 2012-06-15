/*
 * Setup platform devices needed by the Freescale multi-port host
 * and/or dual-role USB controller modules based on the description
 * in flat device tree.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/fsl_devices.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/module.h>

struct fsl_usb2_dev_data {
	char *dr_mode;		/* controller mode */
	char *drivers[3];	/* drivers to instantiate for this mode */
	enum fsl_usb2_operating_modes op_mode;	/* operating mode */
};

struct fsl_usb2_dev_data dr_mode_data[] __devinitdata = {
	{
		.dr_mode = "host",
		.drivers = { "fsl-ehci", NULL, NULL, },
		.op_mode = FSL_USB2_DR_HOST,
	},
	{
		.dr_mode = "otg",
		.drivers = { "fsl-usb2-otg", "fsl-ehci", "fsl-usb2-udc", },
		.op_mode = FSL_USB2_DR_OTG,
	},
	{
		.dr_mode = "peripheral",
		.drivers = { "fsl-usb2-udc", NULL, NULL, },
		.op_mode = FSL_USB2_DR_DEVICE,
	},
};

struct fsl_usb2_dev_data * __devinit get_dr_mode_data(struct device_node *np)
{
	const unsigned char *prop;
	int i;

	prop = of_get_property(np, "dr_mode", NULL);
	if (prop) {
		for (i = 0; i < ARRAY_SIZE(dr_mode_data); i++) {
			if (!strcmp(prop, dr_mode_data[i].dr_mode))
				return &dr_mode_data[i];
		}
	}
	pr_warn("%s: Invalid 'dr_mode' property, fallback to host mode\n",
		np->full_name);
	return &dr_mode_data[0]; /* mode not specified, use host */
}

static enum fsl_usb2_phy_modes __devinit determine_usb_phy(const char *phy_type)
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

struct platform_device * __devinit fsl_usb2_device_register(
					struct platform_device *ofdev,
					struct fsl_usb2_platform_data *pdata,
					const char *name, int id)
{
	struct platform_device *pdev;
	const struct resource *res = ofdev->resource;
	unsigned int num = ofdev->num_resources;
	int retval;

	pdev = platform_device_alloc(name, id);
	if (!pdev) {
		retval = -ENOMEM;
		goto error;
	}

	pdev->dev.parent = &ofdev->dev;

	pdev->dev.coherent_dma_mask = ofdev->dev.coherent_dma_mask;
	*pdev->dev.dma_mask = *ofdev->dev.dma_mask;

	retval = platform_device_add_data(pdev, pdata, sizeof(*pdata));
	if (retval)
		goto error;

	if (num) {
		retval = platform_device_add_resources(pdev, res, num);
		if (retval)
			goto error;
	}

	retval = platform_device_add(pdev);
	if (retval)
		goto error;

	return pdev;

error:
	platform_device_put(pdev);
	return ERR_PTR(retval);
}

static const struct of_device_id fsl_usb2_mph_dr_of_match[];

static int usb_get_ver_info(struct device_node *np)
{
	int ver = -1;

	/*
	 * returns 1 for usb controller version 1.6
	 * returns 2 for usb controller version 2.2
	 * returns 0 otherwise
	 */
	if (of_device_is_compatible(np, "fsl-usb2-dr")) {
		if (of_device_is_compatible(np, "fsl-usb2-dr-v1.6"))
			ver = FSL_USB_VER_1_6;
		else if (of_device_is_compatible(np, "fsl-usb2-dr-v2.2"))
			ver = FSL_USB_VER_2_2;
		else /* for previous controller versions */
			ver = FSL_USB_VER_OLD;

		if (ver > -1)
			return ver;
	}

	if (of_device_is_compatible(np, "fsl-usb2-mph")) {
		if (of_device_is_compatible(np, "fsl-usb2-mph-v1.6"))
			ver = FSL_USB_VER_1_6;
		else if (of_device_is_compatible(np, "fsl-usb2-mph-v2.2"))
			ver = FSL_USB_VER_2_2;
		else /* for previous controller versions */
			ver = FSL_USB_VER_OLD;
	}

	return ver;
}

static int __devinit fsl_usb2_mph_dr_of_probe(struct platform_device *ofdev)
{
	struct device_node *np = ofdev->dev.of_node;
	struct platform_device *usb_dev;
	struct fsl_usb2_platform_data data, *pdata;
	struct fsl_usb2_dev_data *dev_data;
	const struct of_device_id *match;
	const unsigned char *prop;
	static unsigned int idx;
	int i;

	if (!of_device_is_available(np))
		return -ENODEV;

	match = of_match_device(fsl_usb2_mph_dr_of_match, &ofdev->dev);
	if (!match)
		return -ENODEV;

	pdata = &data;
	if (match->data)
		memcpy(pdata, match->data, sizeof(data));
	else
		memset(pdata, 0, sizeof(data));

	dev_data = get_dr_mode_data(np);

	if (of_device_is_compatible(np, "fsl-usb2-mph")) {
		if (of_get_property(np, "port0", NULL))
			pdata->port_enables |= FSL_USB2_PORT0_ENABLED;

		if (of_get_property(np, "port1", NULL))
			pdata->port_enables |= FSL_USB2_PORT1_ENABLED;

		pdata->operating_mode = FSL_USB2_MPH_HOST;
	} else {
		if (of_get_property(np, "fsl,invert-drvvbus", NULL))
			pdata->invert_drvvbus = 1;

		if (of_get_property(np, "fsl,invert-pwr-fault", NULL))
			pdata->invert_pwr_fault = 1;

		/* setup mode selected in the device tree */
		pdata->operating_mode = dev_data->op_mode;
	}

	prop = of_get_property(np, "phy_type", NULL);
	pdata->phy_mode = determine_usb_phy(prop);
	pdata->controller_ver = usb_get_ver_info(np);

	if (pdata->have_sysif_regs) {
		if (pdata->controller_ver < 0) {
			dev_warn(&ofdev->dev, "Could not get controller version\n");
			return -ENODEV;
		}
	}

	for (i = 0; i < ARRAY_SIZE(dev_data->drivers); i++) {
		if (!dev_data->drivers[i])
			continue;
		usb_dev = fsl_usb2_device_register(ofdev, pdata,
					dev_data->drivers[i], idx);
		if (IS_ERR(usb_dev)) {
			dev_err(&ofdev->dev, "Can't register usb device\n");
			return PTR_ERR(usb_dev);
		}
	}
	idx++;
	return 0;
}

static int __devexit __unregister_subdev(struct device *dev, void *d)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int __devexit fsl_usb2_mph_dr_of_remove(struct platform_device *ofdev)
{
	device_for_each_child(&ofdev->dev, NULL, __unregister_subdev);
	return 0;
}

#ifdef CONFIG_PPC_MPC512x

#define USBGENCTRL		0x200		/* NOTE: big endian */
#define GC_WU_INT_CLR		(1 << 5)	/* Wakeup int clear */
#define GC_ULPI_SEL		(1 << 4)	/* ULPI i/f select (usb0 only)*/
#define GC_PPP			(1 << 3)	/* Inv. Port Power Polarity */
#define GC_PFP			(1 << 2)	/* Inv. Power Fault Polarity */
#define GC_WU_ULPI_EN		(1 << 1)	/* Wakeup on ULPI event */
#define GC_WU_IE		(1 << 1)	/* Wakeup interrupt enable */

#define ISIPHYCTRL		0x204		/* NOTE: big endian */
#define PHYCTRL_PHYE		(1 << 4)	/* On-chip UTMI PHY enable */
#define PHYCTRL_BSENH		(1 << 3)	/* Bit Stuff Enable High */
#define PHYCTRL_BSEN		(1 << 2)	/* Bit Stuff Enable */
#define PHYCTRL_LSFE		(1 << 1)	/* Line State Filter Enable */
#define PHYCTRL_PXE		(1 << 0)	/* PHY oscillator enable */

int fsl_usb2_mpc5121_init(struct platform_device *pdev)
{
	struct fsl_usb2_platform_data *pdata = pdev->dev.platform_data;
	struct clk *clk;
	char clk_name[10];
	int base, clk_num;

	base = pdev->resource->start & 0xf000;
	if (base == 0x3000)
		clk_num = 1;
	else if (base == 0x4000)
		clk_num = 2;
	else
		return -ENODEV;

	snprintf(clk_name, sizeof(clk_name), "usb%d_clk", clk_num);
	clk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "failed to get clk\n");
		return PTR_ERR(clk);
	}

	clk_enable(clk);
	pdata->clk = clk;

	if (pdata->phy_mode == FSL_USB2_PHY_UTMI_WIDE) {
		u32 reg = 0;

		if (pdata->invert_drvvbus)
			reg |= GC_PPP;

		if (pdata->invert_pwr_fault)
			reg |= GC_PFP;

		out_be32(pdata->regs + ISIPHYCTRL, PHYCTRL_PHYE | PHYCTRL_PXE);
		out_be32(pdata->regs + USBGENCTRL, reg);
	}
	return 0;
}

static void fsl_usb2_mpc5121_exit(struct platform_device *pdev)
{
	struct fsl_usb2_platform_data *pdata = pdev->dev.platform_data;

	pdata->regs = NULL;

	if (pdata->clk) {
		clk_disable(pdata->clk);
		clk_put(pdata->clk);
	}
}

static struct fsl_usb2_platform_data fsl_usb2_mpc5121_pd = {
	.big_endian_desc = 1,
	.big_endian_mmio = 1,
	.es = 1,
	.have_sysif_regs = 0,
	.le_setup_buf = 1,
	.init = fsl_usb2_mpc5121_init,
	.exit = fsl_usb2_mpc5121_exit,
};
#endif /* CONFIG_PPC_MPC512x */

static struct fsl_usb2_platform_data fsl_usb2_mpc8xxx_pd = {
	.have_sysif_regs = 1,
};

static const struct of_device_id fsl_usb2_mph_dr_of_match[] = {
	{ .compatible = "fsl-usb2-mph", .data = &fsl_usb2_mpc8xxx_pd, },
	{ .compatible = "fsl-usb2-dr", .data = &fsl_usb2_mpc8xxx_pd, },
#ifdef CONFIG_PPC_MPC512x
	{ .compatible = "fsl,mpc5121-usb2-dr", .data = &fsl_usb2_mpc5121_pd, },
#endif
	{},
};

static struct platform_driver fsl_usb2_mph_dr_driver = {
	.driver = {
		.name = "fsl-usb2-mph-dr",
		.owner = THIS_MODULE,
		.of_match_table = fsl_usb2_mph_dr_of_match,
	},
	.probe	= fsl_usb2_mph_dr_of_probe,
	.remove	= __devexit_p(fsl_usb2_mph_dr_of_remove),
};

module_platform_driver(fsl_usb2_mph_dr_driver);

MODULE_DESCRIPTION("FSL MPH DR OF devices driver");
MODULE_AUTHOR("Anatolij Gustschin <agust@denx.de>");
MODULE_LICENSE("GPL");
