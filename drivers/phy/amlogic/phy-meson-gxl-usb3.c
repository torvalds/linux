// SPDX-License-Identifier: GPL-2.0
/*
 * Meson GXL USB3 PHY and OTG mode detection driver
 *
 * Copyright (C) 2018 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
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

#define USB_R0							0x00
	#define USB_R0_P30_FSEL_MASK				GENMASK(5, 0)
	#define USB_R0_P30_PHY_RESET				BIT(6)
	#define USB_R0_P30_TEST_POWERDOWN_HSP			BIT(7)
	#define USB_R0_P30_TEST_POWERDOWN_SSP			BIT(8)
	#define USB_R0_P30_ACJT_LEVEL_MASK			GENMASK(13, 9)
	#define USB_R0_P30_TX_BOOST_LEVEL_MASK			GENMASK(16, 14)
	#define USB_R0_P30_LANE0_TX2RX_LOOPBACK			BIT(17)
	#define USB_R0_P30_LANE0_EXT_PCLK_REQ			BIT(18)
	#define USB_R0_P30_PCS_RX_LOS_MASK_VAL_MASK		GENMASK(28, 19)
	#define USB_R0_U2D_SS_SCALEDOWN_MODE_MASK		GENMASK(30, 29)
	#define USB_R0_U2D_ACT					BIT(31)

#define USB_R1							0x04
	#define USB_R1_U3H_BIGENDIAN_GS				BIT(0)
	#define USB_R1_U3H_PME_ENABLE				BIT(1)
	#define USB_R1_U3H_HUB_PORT_OVERCURRENT_MASK		GENMASK(6, 2)
	#define USB_R1_U3H_HUB_PORT_PERM_ATTACH_MASK		GENMASK(11, 7)
	#define USB_R1_U3H_HOST_U2_PORT_DISABLE_MASK		GENMASK(15, 12)
	#define USB_R1_U3H_HOST_U3_PORT_DISABLE			BIT(16)
	#define USB_R1_U3H_HOST_PORT_POWER_CONTROL_PRESENT	BIT(17)
	#define USB_R1_U3H_HOST_MSI_ENABLE			BIT(18)
	#define USB_R1_U3H_FLADJ_30MHZ_REG_MASK			GENMASK(24, 19)
	#define USB_R1_P30_PCS_TX_SWING_FULL_MASK		GENMASK(31, 25)

#define USB_R2							0x08
	#define USB_R2_P30_CR_DATA_IN_MASK			GENMASK(15, 0)
	#define USB_R2_P30_CR_READ				BIT(16)
	#define USB_R2_P30_CR_WRITE				BIT(17)
	#define USB_R2_P30_CR_CAP_ADDR				BIT(18)
	#define USB_R2_P30_CR_CAP_DATA				BIT(19)
	#define USB_R2_P30_PCS_TX_DEEMPH_3P5DB_MASK		GENMASK(25, 20)
	#define USB_R2_P30_PCS_TX_DEEMPH_6DB_MASK		GENMASK(31, 26)

#define USB_R3							0x0c
	#define USB_R3_P30_SSC_ENABLE				BIT(0)
	#define USB_R3_P30_SSC_RANGE_MASK			GENMASK(3, 1)
	#define USB_R3_P30_SSC_REF_CLK_SEL_MASK			GENMASK(12, 4)
	#define USB_R3_P30_REF_SSP_EN				BIT(13)
	#define USB_R3_P30_LOS_BIAS_MASK			GENMASK(18, 16)
	#define USB_R3_P30_LOS_LEVEL_MASK			GENMASK(23, 19)
	#define USB_R3_P30_MPLL_MULTIPLIER_MASK			GENMASK(30, 24)

#define USB_R4							0x10
	#define USB_R4_P21_PORT_RESET_0				BIT(0)
	#define USB_R4_P21_SLEEP_M0				BIT(1)
	#define USB_R4_MEM_PD_MASK				GENMASK(3, 2)
	#define USB_R4_P21_ONLY					BIT(4)

#define USB_R5							0x14
	#define USB_R5_ID_DIG_SYNC				BIT(0)
	#define USB_R5_ID_DIG_REG				BIT(1)
	#define USB_R5_ID_DIG_CFG_MASK				GENMASK(3, 2)
	#define USB_R5_ID_DIG_EN_0				BIT(4)
	#define USB_R5_ID_DIG_EN_1				BIT(5)
	#define USB_R5_ID_DIG_CURR				BIT(6)
	#define USB_R5_ID_DIG_IRQ				BIT(7)
	#define USB_R5_ID_DIG_TH_MASK				GENMASK(15, 8)
	#define USB_R5_ID_DIG_CNT_MASK				GENMASK(23, 16)

/* read-only register */
#define USB_R6							0x18
	#define USB_R6_P30_CR_DATA_OUT_MASK			GENMASK(15, 0)
	#define USB_R6_P30_CR_ACK				BIT(16)

struct phy_meson_gxl_usb3_priv {
	struct regmap		*regmap;
	enum phy_mode		mode;
	struct clk		*clk_phy;
	struct clk		*clk_peripheral;
	struct reset_control	*reset;
};

static const struct regmap_config phy_meson_gxl_usb3_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = USB_R6,
};

static int phy_meson_gxl_usb3_power_on(struct phy *phy)
{
	struct phy_meson_gxl_usb3_priv *priv = phy_get_drvdata(phy);

	regmap_update_bits(priv->regmap, USB_R5, USB_R5_ID_DIG_EN_0,
			   USB_R5_ID_DIG_EN_0);
	regmap_update_bits(priv->regmap, USB_R5, USB_R5_ID_DIG_EN_1,
			   USB_R5_ID_DIG_EN_1);
	regmap_update_bits(priv->regmap, USB_R5, USB_R5_ID_DIG_TH_MASK,
			   FIELD_PREP(USB_R5_ID_DIG_TH_MASK, 0xff));

	return 0;
}

static int phy_meson_gxl_usb3_power_off(struct phy *phy)
{
	struct phy_meson_gxl_usb3_priv *priv = phy_get_drvdata(phy);

	regmap_update_bits(priv->regmap, USB_R5, USB_R5_ID_DIG_EN_0, 0);
	regmap_update_bits(priv->regmap, USB_R5, USB_R5_ID_DIG_EN_1, 0);

	return 0;
}

static int phy_meson_gxl_usb3_set_mode(struct phy *phy, enum phy_mode mode)
{
	struct phy_meson_gxl_usb3_priv *priv = phy_get_drvdata(phy);

	switch (mode) {
	case PHY_MODE_USB_HOST:
		regmap_update_bits(priv->regmap, USB_R0, USB_R0_U2D_ACT, 0);
		regmap_update_bits(priv->regmap, USB_R4, USB_R4_P21_SLEEP_M0,
				   0);
		break;

	case PHY_MODE_USB_DEVICE:
		regmap_update_bits(priv->regmap, USB_R0, USB_R0_U2D_ACT,
				   USB_R0_U2D_ACT);
		regmap_update_bits(priv->regmap, USB_R4, USB_R4_P21_SLEEP_M0,
				   USB_R4_P21_SLEEP_M0);
		break;

	default:
		dev_err(&phy->dev, "unsupported PHY mode %d\n", mode);
		return -EINVAL;
	}

	priv->mode = mode;

	return 0;
}

static int phy_meson_gxl_usb3_init(struct phy *phy)
{
	struct phy_meson_gxl_usb3_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = reset_control_reset(priv->reset);
	if (ret)
		goto err;

	ret = clk_prepare_enable(priv->clk_phy);
	if (ret)
		goto err;

	ret = clk_prepare_enable(priv->clk_peripheral);
	if (ret)
		goto err_disable_clk_phy;

	ret = phy_meson_gxl_usb3_set_mode(phy, priv->mode);
	if (ret)
		goto err_disable_clk_peripheral;

	regmap_update_bits(priv->regmap, USB_R1,
			   USB_R1_U3H_FLADJ_30MHZ_REG_MASK,
			   FIELD_PREP(USB_R1_U3H_FLADJ_30MHZ_REG_MASK, 0x20));

	return 0;

err_disable_clk_peripheral:
	clk_disable_unprepare(priv->clk_peripheral);
err_disable_clk_phy:
	clk_disable_unprepare(priv->clk_phy);
err:
	return ret;
}

static int phy_meson_gxl_usb3_exit(struct phy *phy)
{
	struct phy_meson_gxl_usb3_priv *priv = phy_get_drvdata(phy);

	clk_disable_unprepare(priv->clk_peripheral);
	clk_disable_unprepare(priv->clk_phy);

	return 0;
}

static const struct phy_ops phy_meson_gxl_usb3_ops = {
	.power_on	= phy_meson_gxl_usb3_power_on,
	.power_off	= phy_meson_gxl_usb3_power_off,
	.set_mode	= phy_meson_gxl_usb3_set_mode,
	.init		= phy_meson_gxl_usb3_init,
	.exit		= phy_meson_gxl_usb3_exit,
	.owner		= THIS_MODULE,
};

static int phy_meson_gxl_usb3_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct phy_meson_gxl_usb3_priv *priv;
	struct resource *res;
	struct phy *phy;
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
					     &phy_meson_gxl_usb3_regmap_conf);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->clk_phy = devm_clk_get(dev, "phy");
	if (IS_ERR(priv->clk_phy))
		return PTR_ERR(priv->clk_phy);

	priv->clk_peripheral = devm_clk_get(dev, "peripheral");
	if (IS_ERR(priv->clk_peripheral))
		return PTR_ERR(priv->clk_peripheral);

	priv->reset = devm_reset_control_array_get_shared(dev);
	if (IS_ERR(priv->reset))
		return PTR_ERR(priv->reset);

	/*
	 * default to host mode as hardware defaults and/or boot-loader
	 * behavior can result in this PHY starting up in device mode. this
	 * default and the initialization in phy_meson_gxl_usb3_init ensure
	 * that we reproducibly start in a known mode on all devices.
	 */
	priv->mode = PHY_MODE_USB_HOST;

	phy = devm_phy_create(dev, np, &phy_meson_gxl_usb3_ops);
	if (IS_ERR(phy)) {
		ret = PTR_ERR(phy);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to create PHY\n");

		return ret;
	}

	phy_set_drvdata(phy, priv);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id phy_meson_gxl_usb3_of_match[] = {
	{ .compatible = "amlogic,meson-gxl-usb3-phy", },
	{ },
};
MODULE_DEVICE_TABLE(of, phy_meson_gxl_usb3_of_match);

static struct platform_driver phy_meson_gxl_usb3_driver = {
	.probe	= phy_meson_gxl_usb3_probe,
	.driver	= {
		.name		= "phy-meson-gxl-usb3",
		.of_match_table	= phy_meson_gxl_usb3_of_match,
	},
};
module_platform_driver(phy_meson_gxl_usb3_driver);

MODULE_AUTHOR("Martin Blumenstingl <martin.blumenstingl@googlemail.com>");
MODULE_DESCRIPTION("Meson GXL USB3 PHY and OTG detection driver");
MODULE_LICENSE("GPL v2");
