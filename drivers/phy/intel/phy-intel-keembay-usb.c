// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay USB PHY driver
 * Copyright (C) 2020 Intel Corporation
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* USS (USB Subsystem) clock control registers */
#define USS_CPR_CLK_EN		0x00
#define USS_CPR_CLK_SET		0x04
#define USS_CPR_CLK_CLR		0x08
#define USS_CPR_RST_EN		0x10
#define USS_CPR_RST_SET		0x14
#define USS_CPR_RST_CLR		0x18

/* USS clock/reset bit fields */
#define USS_CPR_PHY_TST		BIT(6)
#define USS_CPR_LOW_JIT		BIT(5)
#define USS_CPR_CORE		BIT(4)
#define USS_CPR_SUSPEND		BIT(3)
#define USS_CPR_ALT_REF		BIT(2)
#define USS_CPR_REF		BIT(1)
#define USS_CPR_SYS		BIT(0)
#define USS_CPR_MASK		GENMASK(6, 0)

/* USS APB slave registers */
#define USS_USB_CTRL_CFG0		0x10
#define  VCC_RESET_N_MASK		BIT(31)

#define USS_USB_PHY_CFG0		0x30
#define  POR_MASK			BIT(15)
#define  PHY_RESET_MASK			BIT(14)
#define  PHY_REF_USE_PAD_MASK		BIT(5)

#define USS_USB_PHY_CFG6		0x64
#define  PHY0_SRAM_EXT_LD_DONE_MASK	BIT(23)

#define USS_USB_PARALLEL_IF_CTRL	0xa0
#define  USB_PHY_CR_PARA_SEL_MASK	BIT(2)

#define USS_USB_TSET_SIGNALS_AND_GLOB	0xac
#define  USB_PHY_CR_PARA_CLK_EN_MASK	BIT(7)

#define USS_USB_STATUS_REG		0xb8
#define  PHY0_SRAM_INIT_DONE_MASK	BIT(3)

#define USS_USB_TIEOFFS_CONSTANTS_REG1	0xc0
#define  IDDQ_ENABLE_MASK		BIT(10)

struct keembay_usb_phy {
	struct device *dev;
	struct regmap *regmap_cpr;
	struct regmap *regmap_slv;
};

static const struct regmap_config keembay_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = USS_USB_TIEOFFS_CONSTANTS_REG1,
};

static int keembay_usb_clocks_on(struct keembay_usb_phy *priv)
{
	int ret;

	ret = regmap_update_bits(priv->regmap_cpr, USS_CPR_CLK_SET,
				 USS_CPR_MASK, USS_CPR_MASK);
	if (ret) {
		dev_err(priv->dev, "error clock set: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(priv->regmap_cpr, USS_CPR_RST_SET,
				 USS_CPR_MASK, USS_CPR_MASK);
	if (ret) {
		dev_err(priv->dev, "error reset set: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(priv->regmap_slv,
				 USS_USB_TIEOFFS_CONSTANTS_REG1,
				 IDDQ_ENABLE_MASK,
				 FIELD_PREP(IDDQ_ENABLE_MASK, 0));
	if (ret) {
		dev_err(priv->dev, "error iddq disable: %d\n", ret);
		return ret;
	}

	/* Wait 30us to ensure all analog blocks are powered up. */
	usleep_range(30, 60);

	ret = regmap_update_bits(priv->regmap_slv, USS_USB_PHY_CFG0,
				 PHY_REF_USE_PAD_MASK,
				 FIELD_PREP(PHY_REF_USE_PAD_MASK, 1));
	if (ret)
		dev_err(priv->dev, "error ref clock select: %d\n", ret);

	return ret;
}

static int keembay_usb_core_off(struct keembay_usb_phy *priv)
{
	int ret;

	ret = regmap_update_bits(priv->regmap_slv, USS_USB_CTRL_CFG0,
				 VCC_RESET_N_MASK,
				 FIELD_PREP(VCC_RESET_N_MASK, 0));
	if (ret)
		dev_err(priv->dev, "error core reset: %d\n", ret);

	return ret;
}

static int keembay_usb_core_on(struct keembay_usb_phy *priv)
{
	int ret;

	ret = regmap_update_bits(priv->regmap_slv, USS_USB_CTRL_CFG0,
				 VCC_RESET_N_MASK,
				 FIELD_PREP(VCC_RESET_N_MASK, 1));
	if (ret)
		dev_err(priv->dev, "error core on: %d\n", ret);

	return ret;
}

static int keembay_usb_phys_on(struct keembay_usb_phy *priv)
{
	int ret;

	ret = regmap_update_bits(priv->regmap_slv, USS_USB_PHY_CFG0,
				 POR_MASK | PHY_RESET_MASK,
				 FIELD_PREP(POR_MASK | PHY_RESET_MASK, 0));
	if (ret)
		dev_err(priv->dev, "error phys on: %d\n", ret);

	return ret;
}

static int keembay_usb_phy_init(struct phy *phy)
{
	struct keembay_usb_phy *priv = phy_get_drvdata(phy);
	u32 val;
	int ret;

	ret = keembay_usb_core_off(priv);
	if (ret)
		return ret;

	/*
	 * According to Keem Bay datasheet, wait minimum 20us after clock
	 * enable before bringing PHYs out of reset.
	 */
	usleep_range(20, 40);

	ret = keembay_usb_phys_on(priv);
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap_slv,
				 USS_USB_TSET_SIGNALS_AND_GLOB,
				 USB_PHY_CR_PARA_CLK_EN_MASK,
				 FIELD_PREP(USB_PHY_CR_PARA_CLK_EN_MASK, 0));
	if (ret) {
		dev_err(priv->dev, "error cr clock disable: %d\n", ret);
		return ret;
	}

	/*
	 * According to Keem Bay datasheet, wait 2us after disabling the
	 * clock into the USB 3.x parallel interface.
	 */
	udelay(2);

	ret = regmap_update_bits(priv->regmap_slv,
				 USS_USB_PARALLEL_IF_CTRL,
				 USB_PHY_CR_PARA_SEL_MASK,
				 FIELD_PREP(USB_PHY_CR_PARA_SEL_MASK, 1));
	if (ret) {
		dev_err(priv->dev, "error cr select: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(priv->regmap_slv,
				 USS_USB_TSET_SIGNALS_AND_GLOB,
				 USB_PHY_CR_PARA_CLK_EN_MASK,
				 FIELD_PREP(USB_PHY_CR_PARA_CLK_EN_MASK, 1));
	if (ret) {
		dev_err(priv->dev, "error cr clock enable: %d\n", ret);
		return ret;
	}

	ret = regmap_read_poll_timeout(priv->regmap_slv, USS_USB_STATUS_REG,
				       val, val & PHY0_SRAM_INIT_DONE_MASK,
				       USEC_PER_MSEC, 10 * USEC_PER_MSEC);
	if (ret) {
		dev_err(priv->dev, "SRAM init not done: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(priv->regmap_slv, USS_USB_PHY_CFG6,
				 PHY0_SRAM_EXT_LD_DONE_MASK,
				 FIELD_PREP(PHY0_SRAM_EXT_LD_DONE_MASK, 1));
	if (ret) {
		dev_err(priv->dev, "error SRAM init done set: %d\n", ret);
		return ret;
	}

	/*
	 * According to Keem Bay datasheet, wait 20us after setting the
	 * SRAM load done bit, before releasing the controller reset.
	 */
	usleep_range(20, 40);

	return keembay_usb_core_on(priv);
}

static const struct phy_ops ops = {
	.init		= keembay_usb_phy_init,
	.owner		= THIS_MODULE,
};

static int keembay_usb_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct keembay_usb_phy *priv;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;
	void __iomem *base;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	base = devm_platform_ioremap_resource_byname(pdev, "cpr-apb-base");
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->regmap_cpr = devm_regmap_init_mmio(dev, base,
						 &keembay_regmap_config);
	if (IS_ERR(priv->regmap_cpr))
		return PTR_ERR(priv->regmap_cpr);

	base = devm_platform_ioremap_resource_byname(pdev, "slv-apb-base");
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->regmap_slv = devm_regmap_init_mmio(dev, base,
						 &keembay_regmap_config);
	if (IS_ERR(priv->regmap_slv))
		return PTR_ERR(priv->regmap_slv);

	generic_phy = devm_phy_create(dev, dev->of_node, &ops);
	if (IS_ERR(generic_phy))
		return dev_err_probe(dev, PTR_ERR(generic_phy),
				     "failed to create PHY\n");

	phy_set_drvdata(generic_phy, priv);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return dev_err_probe(dev, PTR_ERR(phy_provider),
				     "failed to register phy provider\n");

	/* Setup USB subsystem clocks */
	ret = keembay_usb_clocks_on(priv);
	if (ret)
		return ret;

	/* and turn on the DWC3 core, prior to DWC3 driver init. */
	return keembay_usb_core_on(priv);
}

static const struct of_device_id keembay_usb_phy_dt_ids[] = {
	{ .compatible = "intel,keembay-usb-phy" },
	{}
};
MODULE_DEVICE_TABLE(of, keembay_usb_phy_dt_ids);

static struct platform_driver keembay_usb_phy_driver = {
	.probe		= keembay_usb_phy_probe,
	.driver		= {
		.name	= "keembay-usb-phy",
		.of_match_table = keembay_usb_phy_dt_ids,
	},
};
module_platform_driver(keembay_usb_phy_driver);

MODULE_AUTHOR("Wan Ahmad Zainie <wan.ahmad.zainie.wan.mohamad@intel.com>");
MODULE_DESCRIPTION("Intel Keem Bay USB PHY driver");
MODULE_LICENSE("GPL v2");
