// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * omap-usb2.c - USB PHY, talking to USB controller on TI SoCs.
 *
 * Copyright (C) 2012-2020 Texas Instruments Incorporated - http://www.ti.com
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/phy/omap_control_phy.h>
#include <linux/phy/omap_usb.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>
#include <linux/usb/phy_companion.h>

#define USB2PHY_ANA_CONFIG1		0x4c
#define USB2PHY_DISCON_BYP_LATCH	BIT(31)

#define USB2PHY_CHRG_DET			0x14
#define USB2PHY_CHRG_DET_USE_CHG_DET_REG	BIT(29)
#define USB2PHY_CHRG_DET_DIS_CHG_DET		BIT(28)

/* SoC Specific USB2_OTG register definitions */
#define AM654_USB2_OTG_PD		BIT(8)
#define AM654_USB2_VBUS_DET_EN		BIT(5)
#define AM654_USB2_VBUSVALID_DET_EN	BIT(4)

#define OMAP_DEV_PHY_PD		BIT(0)
#define OMAP_USB2_PHY_PD	BIT(28)

#define AM437X_USB2_PHY_PD		BIT(0)
#define AM437X_USB2_OTG_PD		BIT(1)
#define AM437X_USB2_OTGVDET_EN		BIT(19)
#define AM437X_USB2_OTGSESSEND_EN	BIT(20)

/* Driver Flags */
#define OMAP_USB2_HAS_START_SRP			BIT(0)
#define OMAP_USB2_HAS_SET_VBUS			BIT(1)
#define OMAP_USB2_CALIBRATE_FALSE_DISCONNECT	BIT(2)
#define OMAP_USB2_DISABLE_CHRG_DET		BIT(3)

struct omap_usb {
	struct usb_phy		phy;
	struct phy_companion	*comparator;
	void __iomem		*pll_ctrl_base;
	void __iomem		*phy_base;
	struct device		*dev;
	struct device		*control_dev;
	struct clk		*wkupclk;
	struct clk		*optclk;
	u8			flags;
	struct regmap		*syscon_phy_power; /* ctrl. reg. acces */
	unsigned int		power_reg; /* power reg. index within syscon */
	u32			mask;
	u32			power_on;
	u32			power_off;
};

#define	phy_to_omapusb(x)	container_of((x), struct omap_usb, phy)

struct usb_phy_data {
	const char *label;
	u8 flags;
	u32 mask;
	u32 power_on;
	u32 power_off;
};

static inline u32 omap_usb_readl(void __iomem *addr, unsigned int offset)
{
	return __raw_readl(addr + offset);
}

static inline void omap_usb_writel(void __iomem *addr, unsigned int offset,
				   u32 data)
{
	__raw_writel(data, addr + offset);
}

/**
 * omap_usb2_set_comparator - links the comparator present in the system with
 *	this phy
 * @comparator - the companion phy(comparator) for this phy
 *
 * The phy companion driver should call this API passing the phy_companion
 * filled with set_vbus and start_srp to be used by usb phy.
 *
 * For use by phy companion driver
 */
int omap_usb2_set_comparator(struct phy_companion *comparator)
{
	struct omap_usb	*phy;
	struct usb_phy	*x = usb_get_phy(USB_PHY_TYPE_USB2);

	if (IS_ERR(x))
		return -ENODEV;

	phy = phy_to_omapusb(x);
	phy->comparator = comparator;
	return 0;
}
EXPORT_SYMBOL_GPL(omap_usb2_set_comparator);

static int omap_usb_set_vbus(struct usb_otg *otg, bool enabled)
{
	struct omap_usb *phy = phy_to_omapusb(otg->usb_phy);

	if (!phy->comparator)
		return -ENODEV;

	return phy->comparator->set_vbus(phy->comparator, enabled);
}

static int omap_usb_start_srp(struct usb_otg *otg)
{
	struct omap_usb *phy = phy_to_omapusb(otg->usb_phy);

	if (!phy->comparator)
		return -ENODEV;

	return phy->comparator->start_srp(phy->comparator);
}

static int omap_usb_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	otg->host = host;
	if (!host)
		otg->state = OTG_STATE_UNDEFINED;

	return 0;
}

static int omap_usb_set_peripheral(struct usb_otg *otg,
				   struct usb_gadget *gadget)
{
	otg->gadget = gadget;
	if (!gadget)
		otg->state = OTG_STATE_UNDEFINED;

	return 0;
}

static int omap_usb_phy_power(struct omap_usb *phy, int on)
{
	u32 val;
	int ret;

	if (!phy->syscon_phy_power) {
		omap_control_phy_power(phy->control_dev, on);
		return 0;
	}

	if (on)
		val = phy->power_on;
	else
		val = phy->power_off;

	ret = regmap_update_bits(phy->syscon_phy_power, phy->power_reg,
				 phy->mask, val);
	return ret;
}

static int omap_usb_power_off(struct phy *x)
{
	struct omap_usb *phy = phy_get_drvdata(x);

	return omap_usb_phy_power(phy, false);
}

static int omap_usb_power_on(struct phy *x)
{
	struct omap_usb *phy = phy_get_drvdata(x);

	return omap_usb_phy_power(phy, true);
}

static int omap_usb2_disable_clocks(struct omap_usb *phy)
{
	clk_disable_unprepare(phy->wkupclk);
	if (!IS_ERR(phy->optclk))
		clk_disable_unprepare(phy->optclk);

	return 0;
}

static int omap_usb2_enable_clocks(struct omap_usb *phy)
{
	int ret;

	ret = clk_prepare_enable(phy->wkupclk);
	if (ret < 0) {
		dev_err(phy->dev, "Failed to enable wkupclk %d\n", ret);
		goto err0;
	}

	if (!IS_ERR(phy->optclk)) {
		ret = clk_prepare_enable(phy->optclk);
		if (ret < 0) {
			dev_err(phy->dev, "Failed to enable optclk %d\n", ret);
			goto err1;
		}
	}

	return 0;

err1:
	clk_disable_unprepare(phy->wkupclk);

err0:
	return ret;
}

static int omap_usb_init(struct phy *x)
{
	struct omap_usb *phy = phy_get_drvdata(x);
	u32 val;

	omap_usb2_enable_clocks(phy);

	if (phy->flags & OMAP_USB2_CALIBRATE_FALSE_DISCONNECT) {
		/*
		 *
		 * Reduce the sensitivity of internal PHY by enabling the
		 * DISCON_BYP_LATCH of the USB2PHY_ANA_CONFIG1 register. This
		 * resolves issues with certain devices which can otherwise
		 * be prone to false disconnects.
		 *
		 */
		val = omap_usb_readl(phy->phy_base, USB2PHY_ANA_CONFIG1);
		val |= USB2PHY_DISCON_BYP_LATCH;
		omap_usb_writel(phy->phy_base, USB2PHY_ANA_CONFIG1, val);
	}

	if (phy->flags & OMAP_USB2_DISABLE_CHRG_DET) {
		val = omap_usb_readl(phy->phy_base, USB2PHY_CHRG_DET);
		val |= USB2PHY_CHRG_DET_USE_CHG_DET_REG |
		       USB2PHY_CHRG_DET_DIS_CHG_DET;
		omap_usb_writel(phy->phy_base, USB2PHY_CHRG_DET, val);
	}

	return 0;
}

static int omap_usb_exit(struct phy *x)
{
	struct omap_usb *phy = phy_get_drvdata(x);

	return omap_usb2_disable_clocks(phy);
}

static const struct phy_ops ops = {
	.init		= omap_usb_init,
	.exit		= omap_usb_exit,
	.power_on	= omap_usb_power_on,
	.power_off	= omap_usb_power_off,
	.owner		= THIS_MODULE,
};

static const struct usb_phy_data omap_usb2_data = {
	.label = "omap_usb2",
	.flags = OMAP_USB2_HAS_START_SRP | OMAP_USB2_HAS_SET_VBUS,
	.mask = OMAP_DEV_PHY_PD,
	.power_off = OMAP_DEV_PHY_PD,
};

static const struct usb_phy_data omap5_usb2_data = {
	.label = "omap5_usb2",
	.flags = 0,
	.mask = OMAP_DEV_PHY_PD,
	.power_off = OMAP_DEV_PHY_PD,
};

static const struct usb_phy_data dra7x_usb2_data = {
	.label = "dra7x_usb2",
	.flags = OMAP_USB2_CALIBRATE_FALSE_DISCONNECT,
	.mask = OMAP_DEV_PHY_PD,
	.power_off = OMAP_DEV_PHY_PD,
};

static const struct usb_phy_data dra7x_usb2_phy2_data = {
	.label = "dra7x_usb2_phy2",
	.flags = OMAP_USB2_CALIBRATE_FALSE_DISCONNECT,
	.mask = OMAP_USB2_PHY_PD,
	.power_off = OMAP_USB2_PHY_PD,
};

static const struct usb_phy_data am437x_usb2_data = {
	.label = "am437x_usb2",
	.flags =  0,
	.mask = AM437X_USB2_PHY_PD | AM437X_USB2_OTG_PD |
		AM437X_USB2_OTGVDET_EN | AM437X_USB2_OTGSESSEND_EN,
	.power_on = AM437X_USB2_OTGVDET_EN | AM437X_USB2_OTGSESSEND_EN,
	.power_off = AM437X_USB2_PHY_PD | AM437X_USB2_OTG_PD,
};

static const struct usb_phy_data am654_usb2_data = {
	.label = "am654_usb2",
	.flags = OMAP_USB2_CALIBRATE_FALSE_DISCONNECT,
	.mask = AM654_USB2_OTG_PD | AM654_USB2_VBUS_DET_EN |
		AM654_USB2_VBUSVALID_DET_EN,
	.power_on = AM654_USB2_VBUS_DET_EN | AM654_USB2_VBUSVALID_DET_EN,
	.power_off = AM654_USB2_OTG_PD,
};

static const struct of_device_id omap_usb2_id_table[] = {
	{
		.compatible = "ti,omap-usb2",
		.data = &omap_usb2_data,
	},
	{
		.compatible = "ti,omap5-usb2",
		.data = &omap5_usb2_data,
	},
	{
		.compatible = "ti,dra7x-usb2",
		.data = &dra7x_usb2_data,
	},
	{
		.compatible = "ti,dra7x-usb2-phy2",
		.data = &dra7x_usb2_phy2_data,
	},
	{
		.compatible = "ti,am437x-usb2",
		.data = &am437x_usb2_data,
	},
	{
		.compatible = "ti,am654-usb2",
		.data = &am654_usb2_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, omap_usb2_id_table);

static void omap_usb2_init_errata(struct omap_usb *phy)
{
	static const struct soc_device_attribute am65x_sr10_soc_devices[] = {
		{ .family = "AM65X", .revision = "SR1.0" },
		{ /* sentinel */ }
	};

	/*
	 * Errata i2075: USB2PHY: USB2PHY Charger Detect is Enabled by
	 * Default Without VBUS Presence.
	 *
	 * AM654x SR1.0 has a silicon bug due to which D+ is pulled high after
	 * POR, which could cause enumeration failure with some USB hubs.
	 * Disabling the USB2_PHY Charger Detect function will put D+
	 * into the normal state.
	 */
	if (soc_device_match(am65x_sr10_soc_devices))
		phy->flags |= OMAP_USB2_DISABLE_CHRG_DET;
}

static int omap_usb2_probe(struct platform_device *pdev)
{
	struct omap_usb	*phy;
	struct phy *generic_phy;
	struct resource *res;
	struct phy_provider *phy_provider;
	struct usb_otg *otg;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *control_node;
	struct platform_device *control_pdev;
	const struct of_device_id *of_id;
	struct usb_phy_data *phy_data;

	of_id = of_match_device(omap_usb2_id_table, &pdev->dev);

	if (!of_id)
		return -EINVAL;

	phy_data = (struct usb_phy_data *)of_id->data;

	phy = devm_kzalloc(&pdev->dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	otg = devm_kzalloc(&pdev->dev, sizeof(*otg), GFP_KERNEL);
	if (!otg)
		return -ENOMEM;

	phy->dev		= &pdev->dev;

	phy->phy.dev		= phy->dev;
	phy->phy.label		= phy_data->label;
	phy->phy.otg		= otg;
	phy->phy.type		= USB_PHY_TYPE_USB2;
	phy->mask		= phy_data->mask;
	phy->power_on		= phy_data->power_on;
	phy->power_off		= phy_data->power_off;
	phy->flags		= phy_data->flags;

	omap_usb2_init_errata(phy);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	phy->phy_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(phy->phy_base))
		return PTR_ERR(phy->phy_base);

	phy->syscon_phy_power = syscon_regmap_lookup_by_phandle(node,
								"syscon-phy-power");
	if (IS_ERR(phy->syscon_phy_power)) {
		dev_dbg(&pdev->dev,
			"can't get syscon-phy-power, using control device\n");
		phy->syscon_phy_power = NULL;

		control_node = of_parse_phandle(node, "ctrl-module", 0);
		if (!control_node) {
			dev_err(&pdev->dev,
				"Failed to get control device phandle\n");
			return -EINVAL;
		}

		control_pdev = of_find_device_by_node(control_node);
		if (!control_pdev) {
			dev_err(&pdev->dev, "Failed to get control device\n");
			return -EINVAL;
		}
		phy->control_dev = &control_pdev->dev;
	} else {
		if (of_property_read_u32_index(node,
					       "syscon-phy-power", 1,
					       &phy->power_reg)) {
			dev_err(&pdev->dev,
				"couldn't get power reg. offset\n");
			return -EINVAL;
		}
	}

	phy->wkupclk = devm_clk_get(phy->dev, "wkupclk");
	if (IS_ERR(phy->wkupclk)) {
		if (PTR_ERR(phy->wkupclk) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		dev_warn(&pdev->dev, "unable to get wkupclk %ld, trying old name\n",
			 PTR_ERR(phy->wkupclk));
		phy->wkupclk = devm_clk_get(phy->dev, "usb_phy_cm_clk32k");

		if (IS_ERR(phy->wkupclk)) {
			if (PTR_ERR(phy->wkupclk) != -EPROBE_DEFER)
				dev_err(&pdev->dev, "unable to get usb_phy_cm_clk32k\n");
			return PTR_ERR(phy->wkupclk);
		}

		dev_warn(&pdev->dev,
			 "found usb_phy_cm_clk32k, please fix DTS\n");
	}

	phy->optclk = devm_clk_get(phy->dev, "refclk");
	if (IS_ERR(phy->optclk)) {
		if (PTR_ERR(phy->optclk) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		dev_dbg(&pdev->dev, "unable to get refclk, trying old name\n");
		phy->optclk = devm_clk_get(phy->dev, "usb_otg_ss_refclk960m");

		if (IS_ERR(phy->optclk)) {
			if (PTR_ERR(phy->optclk) != -EPROBE_DEFER) {
				dev_dbg(&pdev->dev,
					"unable to get usb_otg_ss_refclk960m\n");
			}
		} else {
			dev_warn(&pdev->dev,
				 "found usb_otg_ss_refclk960m, please fix DTS\n");
		}
	}

	otg->set_host = omap_usb_set_host;
	otg->set_peripheral = omap_usb_set_peripheral;
	if (phy_data->flags & OMAP_USB2_HAS_SET_VBUS)
		otg->set_vbus = omap_usb_set_vbus;
	if (phy_data->flags & OMAP_USB2_HAS_START_SRP)
		otg->start_srp = omap_usb_start_srp;
	otg->usb_phy = &phy->phy;

	platform_set_drvdata(pdev, phy);
	pm_runtime_enable(phy->dev);

	generic_phy = devm_phy_create(phy->dev, NULL, &ops);
	if (IS_ERR(generic_phy)) {
		pm_runtime_disable(phy->dev);
		return PTR_ERR(generic_phy);
	}

	phy_set_drvdata(generic_phy, phy);
	omap_usb_power_off(generic_phy);

	phy_provider = devm_of_phy_provider_register(phy->dev,
						     of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		pm_runtime_disable(phy->dev);
		return PTR_ERR(phy_provider);
	}

	usb_add_phy_dev(&phy->phy);

	return 0;
}

static int omap_usb2_remove(struct platform_device *pdev)
{
	struct omap_usb	*phy = platform_get_drvdata(pdev);

	usb_remove_phy(&phy->phy);
	pm_runtime_disable(phy->dev);

	return 0;
}

static struct platform_driver omap_usb2_driver = {
	.probe		= omap_usb2_probe,
	.remove		= omap_usb2_remove,
	.driver		= {
		.name	= "omap-usb2",
		.of_match_table = omap_usb2_id_table,
	},
};

module_platform_driver(omap_usb2_driver);

MODULE_ALIAS("platform:omap_usb2");
MODULE_AUTHOR("Texas Instruments Inc.");
MODULE_DESCRIPTION("OMAP USB2 phy driver");
MODULE_LICENSE("GPL v2");
