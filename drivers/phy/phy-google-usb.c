// SPDX-License-Identifier: GPL-2.0
/*
 * phy-google-usb.c - Google USB PHY driver
 *
 * Copyright (C) 2025, Google LLC
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/usb/typec_mux.h>

#define USBCS_USB2PHY_CFG19_OFFSET 0x0
#define USBCS_USB2PHY_CFG19_PHY_CFG_PLL_FB_DIV GENMASK(19, 8)

#define USBCS_USB2PHY_CFG21_OFFSET 0x8
#define USBCS_USB2PHY_CFG21_PHY_ENABLE BIT(12)
#define USBCS_USB2PHY_CFG21_REF_FREQ_SEL GENMASK(15, 13)
#define USBCS_USB2PHY_CFG21_PHY_TX_DIG_BYPASS_SEL BIT(19)

#define USBCS_PHY_CFG1_OFFSET 0x28
#define USBCS_PHY_CFG1_SYS_VBUSVALID BIT(17)

enum google_usb_phy_id {
	GOOGLE_USB2_PHY,
	GOOGLE_USB_PHY_NUM,
};

struct google_usb_phy_instance {
	struct google_usb_phy *parent;
	unsigned int index;
	struct phy *phy;
	unsigned int num_clks;
	struct clk_bulk_data *clks;
	unsigned int num_rsts;
	struct reset_control_bulk_data *rsts;
};

struct google_usb_phy {
	struct device *dev;
	struct regmap *usb_cfg_regmap;
	unsigned int usb2_cfg_offset;
	void __iomem *usbdp_top_base;
	struct google_usb_phy_instance *insts;
	/*
	 * Protect phy registers from concurrent access, specifically via
	 * google_usb_set_orientation callback.
	 */
	struct mutex phy_mutex;
	struct typec_switch_dev *sw;
	enum typec_orientation orientation;
};

static void set_vbus_valid(struct google_usb_phy *gphy)
{
	u32 reg;

	if (gphy->orientation == TYPEC_ORIENTATION_NONE) {
		reg = readl(gphy->usbdp_top_base + USBCS_PHY_CFG1_OFFSET);
		reg &= ~USBCS_PHY_CFG1_SYS_VBUSVALID;
		writel(reg, gphy->usbdp_top_base + USBCS_PHY_CFG1_OFFSET);
	} else {
		reg = readl(gphy->usbdp_top_base + USBCS_PHY_CFG1_OFFSET);
		reg |= USBCS_PHY_CFG1_SYS_VBUSVALID;
		writel(reg, gphy->usbdp_top_base + USBCS_PHY_CFG1_OFFSET);
	}
}

static int google_usb_set_orientation(struct typec_switch_dev *sw,
				      enum typec_orientation orientation)
{
	struct google_usb_phy *gphy = typec_switch_get_drvdata(sw);

	dev_dbg(gphy->dev, "set orientation %d\n", orientation);

	gphy->orientation = orientation;

	if (pm_runtime_suspended(gphy->dev))
		return 0;

	guard(mutex)(&gphy->phy_mutex);

	set_vbus_valid(gphy);

	return 0;
}

static int google_usb2_phy_init(struct phy *_phy)
{
	struct google_usb_phy_instance *inst = phy_get_drvdata(_phy);
	struct google_usb_phy *gphy = inst->parent;
	u32 reg;
	int ret;

	dev_dbg(gphy->dev, "initializing usb2 phy\n");

	guard(mutex)(&gphy->phy_mutex);

	regmap_read(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG21_OFFSET, &reg);
	reg &= ~USBCS_USB2PHY_CFG21_PHY_TX_DIG_BYPASS_SEL;
	reg &= ~USBCS_USB2PHY_CFG21_REF_FREQ_SEL;
	reg |= FIELD_PREP(USBCS_USB2PHY_CFG21_REF_FREQ_SEL, 0);
	regmap_write(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG21_OFFSET, reg);

	regmap_read(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG19_OFFSET, &reg);
	reg &= ~USBCS_USB2PHY_CFG19_PHY_CFG_PLL_FB_DIV;
	reg |= FIELD_PREP(USBCS_USB2PHY_CFG19_PHY_CFG_PLL_FB_DIV, 368);
	regmap_write(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG19_OFFSET, reg);

	set_vbus_valid(gphy);

	ret = clk_bulk_prepare_enable(inst->num_clks, inst->clks);
	if (ret)
		return ret;

	ret = reset_control_bulk_deassert(inst->num_rsts, inst->rsts);
	if (ret) {
		clk_bulk_disable_unprepare(inst->num_clks, inst->clks);
		return ret;
	}

	regmap_read(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG21_OFFSET, &reg);
	reg |= USBCS_USB2PHY_CFG21_PHY_ENABLE;
	regmap_write(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG21_OFFSET, reg);

	return 0;
}

static int google_usb2_phy_exit(struct phy *_phy)
{
	struct google_usb_phy_instance *inst = phy_get_drvdata(_phy);
	struct google_usb_phy *gphy = inst->parent;
	u32 reg;

	dev_dbg(gphy->dev, "exiting usb2 phy\n");

	guard(mutex)(&gphy->phy_mutex);

	regmap_read(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG21_OFFSET, &reg);
	reg &= ~USBCS_USB2PHY_CFG21_PHY_ENABLE;
	regmap_write(gphy->usb_cfg_regmap, gphy->usb2_cfg_offset + USBCS_USB2PHY_CFG21_OFFSET, reg);

	reset_control_bulk_assert(inst->num_rsts, inst->rsts);
	clk_bulk_disable_unprepare(inst->num_clks, inst->clks);

	return 0;
}

static const struct phy_ops google_usb2_phy_ops = {
	.init		= google_usb2_phy_init,
	.exit		= google_usb2_phy_exit,
};

static struct phy *google_usb_phy_xlate(struct device *dev,
					const struct of_phandle_args *args)
{
	struct google_usb_phy *gphy = dev_get_drvdata(dev);

	if (args->args[0] >= GOOGLE_USB_PHY_NUM) {
		dev_err(dev, "invalid PHY index requested from DT\n");
		return ERR_PTR(-ENODEV);
	}
	return gphy->insts[args->args[0]].phy;
}

static int google_usb_phy_probe(struct platform_device *pdev)
{
	struct typec_switch_desc sw_desc = { };
	struct google_usb_phy_instance *inst;
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct google_usb_phy *gphy;
	struct phy *phy;
	u32 args[1];
	int ret;

	gphy = devm_kzalloc(dev, sizeof(*gphy), GFP_KERNEL);
	if (!gphy)
		return -ENOMEM;

	dev_set_drvdata(dev, gphy);
	gphy->dev = dev;

	ret = devm_mutex_init(dev, &gphy->phy_mutex);
	if (ret)
		return ret;

	gphy->usb_cfg_regmap =
		syscon_regmap_lookup_by_phandle_args(dev->of_node,
						     "google,usb-cfg-csr",
						     ARRAY_SIZE(args), args);
	if (IS_ERR(gphy->usb_cfg_regmap)) {
		return dev_err_probe(dev, PTR_ERR(gphy->usb_cfg_regmap),
				     "invalid usb cfg csr\n");
	}

	gphy->usb2_cfg_offset = args[0];

	gphy->usbdp_top_base = devm_platform_ioremap_resource_byname(pdev,
								     "usbdp_top");
	if (IS_ERR(gphy->usbdp_top_base))
		return dev_err_probe(dev, PTR_ERR(gphy->usbdp_top_base),
				    "invalid usbdp top\n");

	gphy->insts = devm_kcalloc(dev, GOOGLE_USB_PHY_NUM, sizeof(*gphy->insts), GFP_KERNEL);
	if (!gphy->insts)
		return -ENOMEM;

	inst = &gphy->insts[GOOGLE_USB2_PHY];
	inst->parent = gphy;
	inst->index = GOOGLE_USB2_PHY;
	phy = devm_phy_create(dev, NULL, &google_usb2_phy_ops);
	if (IS_ERR(phy))
		return dev_err_probe(dev, PTR_ERR(phy),
				     "failed to create usb2 phy instance\n");
	inst->phy = phy;
	phy_set_drvdata(phy, inst);

	inst->num_clks = 2;
	inst->clks = devm_kcalloc(dev, inst->num_clks, sizeof(*inst->clks), GFP_KERNEL);
	if (!inst->clks)
		return -ENOMEM;
	inst->clks[0].id = "usb2";
	inst->clks[1].id = "usb2_apb";
	ret = devm_clk_bulk_get(dev, inst->num_clks, inst->clks);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get u2 phy clks\n");

	inst->num_rsts = 2;
	inst->rsts = devm_kcalloc(dev, inst->num_rsts, sizeof(*inst->rsts), GFP_KERNEL);
	if (!inst->rsts)
		return -ENOMEM;
	inst->rsts[0].id = "usb2";
	inst->rsts[1].id = "usb2_apb";
	ret = devm_reset_control_bulk_get_exclusive(dev, inst->num_rsts, inst->rsts);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get u2 phy resets\n");

	phy_provider = devm_of_phy_provider_register(dev, google_usb_phy_xlate);
	if (IS_ERR(phy_provider))
		return dev_err_probe(dev, PTR_ERR(phy_provider),
				     "failed to register phy provider\n");

	pm_runtime_enable(dev);

	sw_desc.fwnode = dev_fwnode(dev);
	sw_desc.drvdata = gphy;
	sw_desc.name = fwnode_get_name(dev_fwnode(dev));
	sw_desc.set = google_usb_set_orientation;

	gphy->sw = typec_switch_register(dev, &sw_desc);
	if (IS_ERR(gphy->sw))
		return dev_err_probe(dev, PTR_ERR(gphy->sw),
				     "failed to register typec switch\n");

	return 0;
}

static void google_usb_phy_remove(struct platform_device *pdev)
{
	struct google_usb_phy *gphy = dev_get_drvdata(&pdev->dev);

	typec_switch_unregister(gphy->sw);
	pm_runtime_disable(&pdev->dev);
}

static const struct of_device_id google_usb_phy_of_match[] = {
	{
		.compatible = "google,lga-usb-phy",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, google_usb_phy_of_match);

static struct platform_driver google_usb_phy = {
	.probe	= google_usb_phy_probe,
	.remove = google_usb_phy_remove,
	.driver = {
		.name		= "google-usb-phy",
		.of_match_table	= google_usb_phy_of_match,
	}
};

module_platform_driver(google_usb_phy);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Google USB phy driver");
