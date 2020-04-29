// SPDX-License-Identifier: GPL-2.0
/*
 * Amlogic G12A USB3 + PCIE Combo PHY driver
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved
 * Copyright (C) 2019 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/platform_device.h>
#include <dt-bindings/phy/phy.h>

#define PHY_R0							0x00
	#define PHY_R0_PCIE_POWER_STATE				GENMASK(4, 0)
	#define PHY_R0_PCIE_USB3_SWITCH				GENMASK(6, 5)

#define PHY_R1							0x04
	#define PHY_R1_PHY_TX1_TERM_OFFSET			GENMASK(4, 0)
	#define PHY_R1_PHY_TX0_TERM_OFFSET			GENMASK(9, 5)
	#define PHY_R1_PHY_RX1_EQ				GENMASK(12, 10)
	#define PHY_R1_PHY_RX0_EQ				GENMASK(15, 13)
	#define PHY_R1_PHY_LOS_LEVEL				GENMASK(20, 16)
	#define PHY_R1_PHY_LOS_BIAS				GENMASK(23, 21)
	#define PHY_R1_PHY_REF_CLKDIV2				BIT(24)
	#define PHY_R1_PHY_MPLL_MULTIPLIER			GENMASK(31, 25)

#define PHY_R2							0x08
	#define PHY_R2_PCS_TX_DEEMPH_GEN2_6DB			GENMASK(5, 0)
	#define PHY_R2_PCS_TX_DEEMPH_GEN2_3P5DB			GENMASK(11, 6)
	#define PHY_R2_PCS_TX_DEEMPH_GEN1			GENMASK(17, 12)
	#define PHY_R2_PHY_TX_VBOOST_LVL			GENMASK(20, 18)

#define PHY_R4							0x10
	#define PHY_R4_PHY_CR_WRITE				BIT(0)
	#define PHY_R4_PHY_CR_READ				BIT(1)
	#define PHY_R4_PHY_CR_DATA_IN				GENMASK(17, 2)
	#define PHY_R4_PHY_CR_CAP_DATA				BIT(18)
	#define PHY_R4_PHY_CR_CAP_ADDR				BIT(19)

#define PHY_R5							0x14
	#define PHY_R5_PHY_CR_DATA_OUT				GENMASK(15, 0)
	#define PHY_R5_PHY_CR_ACK				BIT(16)
	#define PHY_R5_PHY_BS_OUT				BIT(17)

#define PCIE_RESET_DELAY					500

struct phy_g12a_usb3_pcie_priv {
	struct regmap		*regmap;
	struct regmap		*regmap_cr;
	struct clk		*clk_ref;
	struct reset_control	*reset;
	struct phy		*phy;
	unsigned int		mode;
};

static const struct regmap_config phy_g12a_usb3_pcie_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = PHY_R5,
};

static int phy_g12a_usb3_pcie_cr_bus_addr(struct phy_g12a_usb3_pcie_priv *priv,
					  unsigned int addr)
{
	unsigned int val, reg;
	int ret;

	reg = FIELD_PREP(PHY_R4_PHY_CR_DATA_IN, addr);

	regmap_write(priv->regmap, PHY_R4, reg);
	regmap_write(priv->regmap, PHY_R4, reg);

	regmap_write(priv->regmap, PHY_R4, reg | PHY_R4_PHY_CR_CAP_ADDR);

	ret = regmap_read_poll_timeout(priv->regmap, PHY_R5, val,
				       (val & PHY_R5_PHY_CR_ACK),
				       5, 1000);
	if (ret)
		return ret;

	regmap_write(priv->regmap, PHY_R4, reg);

	ret = regmap_read_poll_timeout(priv->regmap, PHY_R5, val,
				       !(val & PHY_R5_PHY_CR_ACK),
				       5, 1000);
	if (ret)
		return ret;

	return 0;
}

static int phy_g12a_usb3_pcie_cr_bus_read(void *context, unsigned int addr,
					  unsigned int *data)
{
	struct phy_g12a_usb3_pcie_priv *priv = context;
	unsigned int val;
	int ret;

	ret = phy_g12a_usb3_pcie_cr_bus_addr(priv, addr);
	if (ret)
		return ret;

	regmap_write(priv->regmap, PHY_R4, 0);
	regmap_write(priv->regmap, PHY_R4, PHY_R4_PHY_CR_READ);

	ret = regmap_read_poll_timeout(priv->regmap, PHY_R5, val,
				       (val & PHY_R5_PHY_CR_ACK),
				       5, 1000);
	if (ret)
		return ret;

	*data = FIELD_GET(PHY_R5_PHY_CR_DATA_OUT, val);

	regmap_write(priv->regmap, PHY_R4, 0);

	ret = regmap_read_poll_timeout(priv->regmap, PHY_R5, val,
				       !(val & PHY_R5_PHY_CR_ACK),
				       5, 1000);
	if (ret)
		return ret;

	return 0;
}

static int phy_g12a_usb3_pcie_cr_bus_write(void *context, unsigned int addr,
					   unsigned int data)
{
	struct phy_g12a_usb3_pcie_priv *priv = context;
	unsigned int val, reg;
	int ret;

	ret = phy_g12a_usb3_pcie_cr_bus_addr(priv, addr);
	if (ret)
		return ret;

	reg = FIELD_PREP(PHY_R4_PHY_CR_DATA_IN, data);

	regmap_write(priv->regmap, PHY_R4, reg);
	regmap_write(priv->regmap, PHY_R4, reg);

	regmap_write(priv->regmap, PHY_R4, reg | PHY_R4_PHY_CR_CAP_DATA);

	ret = regmap_read_poll_timeout(priv->regmap, PHY_R5, val,
				       (val & PHY_R5_PHY_CR_ACK),
				       5, 1000);
	if (ret)
		return ret;

	regmap_write(priv->regmap, PHY_R4, reg);

	ret = regmap_read_poll_timeout(priv->regmap, PHY_R5, val,
				       (val & PHY_R5_PHY_CR_ACK) == 0,
				       5, 1000);
	if (ret)
		return ret;

	regmap_write(priv->regmap, PHY_R4, reg);

	regmap_write(priv->regmap, PHY_R4, reg | PHY_R4_PHY_CR_WRITE);

	ret = regmap_read_poll_timeout(priv->regmap, PHY_R5, val,
				       (val & PHY_R5_PHY_CR_ACK),
				       5, 1000);
	if (ret)
		return ret;

	regmap_write(priv->regmap, PHY_R4, reg);

	ret = regmap_read_poll_timeout(priv->regmap, PHY_R5, val,
				       (val & PHY_R5_PHY_CR_ACK) == 0,
				       5, 1000);
	if (ret)
		return ret;

	return 0;
}

static const struct regmap_config phy_g12a_usb3_pcie_cr_regmap_conf = {
	.reg_bits = 16,
	.val_bits = 16,
	.reg_read = phy_g12a_usb3_pcie_cr_bus_read,
	.reg_write = phy_g12a_usb3_pcie_cr_bus_write,
	.max_register = 0xffff,
	.disable_locking = true,
};

static int phy_g12a_usb3_init(struct phy *phy)
{
	struct phy_g12a_usb3_pcie_priv *priv = phy_get_drvdata(phy);
	int data, ret;

	ret = reset_control_reset(priv->reset);
	if (ret)
		return ret;

	/* Switch PHY to USB3 */
	/* TODO figure out how to handle when PCIe was set in the bootloader */
	regmap_update_bits(priv->regmap, PHY_R0,
			   PHY_R0_PCIE_USB3_SWITCH,
			   PHY_R0_PCIE_USB3_SWITCH);

	/*
	 * WORKAROUND: There is SSPHY suspend bug due to
	 * which USB enumerates
	 * in HS mode instead of SS mode. Workaround it by asserting
	 * LANE0.TX_ALT_BLOCK.EN_ALT_BUS to enable TX to use alt bus
	 * mode
	 */
	ret = regmap_update_bits(priv->regmap_cr, 0x102d, BIT(7), BIT(7));
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap_cr, 0x1010, 0xff0, 20);
	if (ret)
		return ret;

	/*
	 * Fix RX Equalization setting as follows
	 * LANE0.RX_OVRD_IN_HI. RX_EQ_EN set to 0
	 * LANE0.RX_OVRD_IN_HI.RX_EQ_EN_OVRD set to 1
	 * LANE0.RX_OVRD_IN_HI.RX_EQ set to 3
	 * LANE0.RX_OVRD_IN_HI.RX_EQ_OVRD set to 1
	 */
	ret = regmap_read(priv->regmap_cr, 0x1006, &data);
	if (ret)
		return ret;

	data &= ~BIT(6);
	data |= BIT(7);
	data &= ~(0x7 << 8);
	data |= (0x3 << 8);
	data |= (1 << 11);
	ret = regmap_write(priv->regmap_cr, 0x1006, data);
	if (ret)
		return ret;

	/*
	 * Set EQ and TX launch amplitudes as follows
	 * LANE0.TX_OVRD_DRV_LO.PREEMPH set to 22
	 * LANE0.TX_OVRD_DRV_LO.AMPLITUDE set to 127
	 * LANE0.TX_OVRD_DRV_LO.EN set to 1.
	 */
	ret = regmap_read(priv->regmap_cr, 0x1002, &data);
	if (ret)
		return ret;

	data &= ~0x3f80;
	data |= (0x16 << 7);
	data &= ~0x7f;
	data |= (0x7f | BIT(14));
	ret = regmap_write(priv->regmap_cr, 0x1002, data);
	if (ret)
		return ret;

	/* MPLL_LOOP_CTL.PROP_CNTRL = 8 */
	ret = regmap_update_bits(priv->regmap_cr, 0x30, 0xf << 4, 8 << 4);
	if (ret)
		return ret;

	regmap_update_bits(priv->regmap, PHY_R2,
			PHY_R2_PHY_TX_VBOOST_LVL,
			FIELD_PREP(PHY_R2_PHY_TX_VBOOST_LVL, 0x4));

	regmap_update_bits(priv->regmap, PHY_R1,
			PHY_R1_PHY_LOS_BIAS | PHY_R1_PHY_LOS_LEVEL,
			FIELD_PREP(PHY_R1_PHY_LOS_BIAS, 4) |
			FIELD_PREP(PHY_R1_PHY_LOS_LEVEL, 9));

	return 0;
}

static int phy_g12a_usb3_pcie_power_on(struct phy *phy)
{
	struct phy_g12a_usb3_pcie_priv *priv = phy_get_drvdata(phy);

	if (priv->mode == PHY_TYPE_USB3)
		return 0;

	regmap_update_bits(priv->regmap, PHY_R0,
			   PHY_R0_PCIE_POWER_STATE,
			   FIELD_PREP(PHY_R0_PCIE_POWER_STATE, 0x1c));

	return 0;
}

static int phy_g12a_usb3_pcie_power_off(struct phy *phy)
{
	struct phy_g12a_usb3_pcie_priv *priv = phy_get_drvdata(phy);

	if (priv->mode == PHY_TYPE_USB3)
		return 0;

	regmap_update_bits(priv->regmap, PHY_R0,
			   PHY_R0_PCIE_POWER_STATE,
			   FIELD_PREP(PHY_R0_PCIE_POWER_STATE, 0x1d));

	return 0;
}

static int phy_g12a_usb3_pcie_reset(struct phy *phy)
{
	struct phy_g12a_usb3_pcie_priv *priv = phy_get_drvdata(phy);
	int ret;

	if (priv->mode == PHY_TYPE_USB3)
		return 0;

	ret = reset_control_assert(priv->reset);
	if (ret)
		return ret;

	udelay(PCIE_RESET_DELAY);

	ret = reset_control_deassert(priv->reset);
	if (ret)
		return ret;

	udelay(PCIE_RESET_DELAY);

	return 0;
}

static int phy_g12a_usb3_pcie_init(struct phy *phy)
{
	struct phy_g12a_usb3_pcie_priv *priv = phy_get_drvdata(phy);

	if (priv->mode == PHY_TYPE_USB3)
		return phy_g12a_usb3_init(phy);

	return 0;
}

static int phy_g12a_usb3_pcie_exit(struct phy *phy)
{
	struct phy_g12a_usb3_pcie_priv *priv = phy_get_drvdata(phy);

	if (priv->mode == PHY_TYPE_USB3)
		return reset_control_reset(priv->reset);

	return 0;
}

static struct phy *phy_g12a_usb3_pcie_xlate(struct device *dev,
					    struct of_phandle_args *args)
{
	struct phy_g12a_usb3_pcie_priv *priv = dev_get_drvdata(dev);
	unsigned int mode;

	if (args->args_count < 1) {
		dev_err(dev, "invalid number of arguments\n");
		return ERR_PTR(-EINVAL);
	}

	mode = args->args[0];

	if (mode != PHY_TYPE_USB3 && mode != PHY_TYPE_PCIE) {
		dev_err(dev, "invalid phy mode select argument\n");
		return ERR_PTR(-EINVAL);
	}

	priv->mode = mode;

	return priv->phy;
}

static const struct phy_ops phy_g12a_usb3_pcie_ops = {
	.init		= phy_g12a_usb3_pcie_init,
	.exit		= phy_g12a_usb3_pcie_exit,
	.power_on	= phy_g12a_usb3_pcie_power_on,
	.power_off	= phy_g12a_usb3_pcie_power_off,
	.reset		= phy_g12a_usb3_pcie_reset,
	.owner		= THIS_MODULE,
};

static int phy_g12a_usb3_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct phy_g12a_usb3_pcie_priv *priv;
	struct resource *res;
	struct phy_provider *phy_provider;
	void __iomem *base;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->regmap = devm_regmap_init_mmio(dev, base,
					     &phy_g12a_usb3_pcie_regmap_conf);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->regmap_cr = devm_regmap_init(dev, NULL, priv,
					   &phy_g12a_usb3_pcie_cr_regmap_conf);
	if (IS_ERR(priv->regmap_cr))
		return PTR_ERR(priv->regmap_cr);

	priv->clk_ref = devm_clk_get(dev, "ref_clk");
	if (IS_ERR(priv->clk_ref))
		return PTR_ERR(priv->clk_ref);

	ret = clk_prepare_enable(priv->clk_ref);
	if (ret)
		goto err_disable_clk_ref;

	priv->reset = devm_reset_control_array_get(dev, false, false);
	if (IS_ERR(priv->reset))
		return PTR_ERR(priv->reset);

	priv->phy = devm_phy_create(dev, np, &phy_g12a_usb3_pcie_ops);
	if (IS_ERR(priv->phy)) {
		ret = PTR_ERR(priv->phy);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to create PHY\n");

		return ret;
	}

	phy_set_drvdata(priv->phy, priv);
	dev_set_drvdata(dev, priv);

	phy_provider = devm_of_phy_provider_register(dev,
						     phy_g12a_usb3_pcie_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);

err_disable_clk_ref:
	clk_disable_unprepare(priv->clk_ref);

	return ret;
}

static const struct of_device_id phy_g12a_usb3_pcie_of_match[] = {
	{ .compatible = "amlogic,g12a-usb3-pcie-phy", },
	{ },
};
MODULE_DEVICE_TABLE(of, phy_g12a_usb3_pcie_of_match);

static struct platform_driver phy_g12a_usb3_pcie_driver = {
	.probe	= phy_g12a_usb3_pcie_probe,
	.driver	= {
		.name		= "phy-g12a-usb3-pcie",
		.of_match_table	= phy_g12a_usb3_pcie_of_match,
	},
};
module_platform_driver(phy_g12a_usb3_pcie_driver);

MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_DESCRIPTION("Amlogic G12A USB3 + PCIE Combo PHY driver");
MODULE_LICENSE("GPL v2");
