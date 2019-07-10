// SPDX-License-Identifier: GPL-2.0
/*
 * Meson G12A USB2 PHY driver
 *
 * Copyright (C) 2017 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved
 * Copyright (C) 2019 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#define PHY_CTRL_R0						0x0
#define PHY_CTRL_R1						0x4
#define PHY_CTRL_R2						0x8
#define PHY_CTRL_R3						0xc
	#define PHY_CTRL_R3_SQUELCH_REF				GENMASK(1, 0)
	#define PHY_CTRL_R3_HSDIC_REF				GENMASK(3, 2)
	#define PHY_CTRL_R3_DISC_THRESH				GENMASK(7, 4)

#define PHY_CTRL_R4						0x10
	#define PHY_CTRL_R4_CALIB_CODE_7_0			GENMASK(7, 0)
	#define PHY_CTRL_R4_CALIB_CODE_15_8			GENMASK(15, 8)
	#define PHY_CTRL_R4_CALIB_CODE_23_16			GENMASK(23, 16)
	#define PHY_CTRL_R4_I_C2L_CAL_EN			BIT(24)
	#define PHY_CTRL_R4_I_C2L_CAL_RESET_N			BIT(25)
	#define PHY_CTRL_R4_I_C2L_CAL_DONE			BIT(26)
	#define PHY_CTRL_R4_TEST_BYPASS_MODE_EN			BIT(27)
	#define PHY_CTRL_R4_I_C2L_BIAS_TRIM_1_0			GENMASK(29, 28)
	#define PHY_CTRL_R4_I_C2L_BIAS_TRIM_3_2			GENMASK(31, 30)

#define PHY_CTRL_R5						0x14
#define PHY_CTRL_R6						0x18
#define PHY_CTRL_R7						0x1c
#define PHY_CTRL_R8						0x20
#define PHY_CTRL_R9						0x24
#define PHY_CTRL_R10						0x28
#define PHY_CTRL_R11						0x2c
#define PHY_CTRL_R12						0x30
#define PHY_CTRL_R13						0x34
	#define PHY_CTRL_R13_CUSTOM_PATTERN_19			GENMASK(7, 0)
	#define PHY_CTRL_R13_LOAD_STAT				BIT(14)
	#define PHY_CTRL_R13_UPDATE_PMA_SIGNALS			BIT(15)
	#define PHY_CTRL_R13_MIN_COUNT_FOR_SYNC_DET		GENMASK(20, 16)
	#define PHY_CTRL_R13_CLEAR_HOLD_HS_DISCONNECT		BIT(21)
	#define PHY_CTRL_R13_BYPASS_HOST_DISCONNECT_VAL		BIT(22)
	#define PHY_CTRL_R13_BYPASS_HOST_DISCONNECT_EN		BIT(23)
	#define PHY_CTRL_R13_I_C2L_HS_EN			BIT(24)
	#define PHY_CTRL_R13_I_C2L_FS_EN			BIT(25)
	#define PHY_CTRL_R13_I_C2L_LS_EN			BIT(26)
	#define PHY_CTRL_R13_I_C2L_HS_OE			BIT(27)
	#define PHY_CTRL_R13_I_C2L_FS_OE			BIT(28)
	#define PHY_CTRL_R13_I_C2L_HS_RX_EN			BIT(29)
	#define PHY_CTRL_R13_I_C2L_FSLS_RX_EN			BIT(30)

#define PHY_CTRL_R14						0x38
	#define PHY_CTRL_R14_I_RDP_EN				BIT(0)
	#define PHY_CTRL_R14_I_RPU_SW1_EN			BIT(1)
	#define PHY_CTRL_R14_I_RPU_SW2_EN			GENMASK(3, 2)
	#define PHY_CTRL_R14_PG_RSTN				BIT(4)
	#define PHY_CTRL_R14_I_C2L_DATA_16_8			BIT(5)
	#define PHY_CTRL_R14_I_C2L_ASSERT_SINGLE_EN_ZERO	BIT(6)
	#define PHY_CTRL_R14_BYPASS_CTRL_7_0			GENMASK(15, 8)
	#define PHY_CTRL_R14_BYPASS_CTRL_15_8			GENMASK(23, 16)

#define PHY_CTRL_R15						0x3c
#define PHY_CTRL_R16						0x40
	#define PHY_CTRL_R16_MPLL_M				GENMASK(8, 0)
	#define PHY_CTRL_R16_MPLL_N				GENMASK(14, 10)
	#define PHY_CTRL_R16_MPLL_TDC_MODE			BIT(20)
	#define PHY_CTRL_R16_MPLL_SDM_EN			BIT(21)
	#define PHY_CTRL_R16_MPLL_LOAD				BIT(22)
	#define PHY_CTRL_R16_MPLL_DCO_SDM_EN			BIT(23)
	#define PHY_CTRL_R16_MPLL_LOCK_LONG			GENMASK(25, 24)
	#define PHY_CTRL_R16_MPLL_LOCK_F			BIT(26)
	#define PHY_CTRL_R16_MPLL_FAST_LOCK			BIT(27)
	#define PHY_CTRL_R16_MPLL_EN				BIT(28)
	#define PHY_CTRL_R16_MPLL_RESET				BIT(29)
	#define PHY_CTRL_R16_MPLL_LOCK				BIT(30)
	#define PHY_CTRL_R16_MPLL_LOCK_DIG			BIT(31)

#define PHY_CTRL_R17						0x44
	#define PHY_CTRL_R17_MPLL_FRAC_IN			GENMASK(13, 0)
	#define PHY_CTRL_R17_MPLL_FIX_EN			BIT(16)
	#define PHY_CTRL_R17_MPLL_LAMBDA1			GENMASK(19, 17)
	#define PHY_CTRL_R17_MPLL_LAMBDA0			GENMASK(22, 20)
	#define PHY_CTRL_R17_MPLL_FILTER_MODE			BIT(23)
	#define PHY_CTRL_R17_MPLL_FILTER_PVT2			GENMASK(27, 24)
	#define PHY_CTRL_R17_MPLL_FILTER_PVT1			GENMASK(31, 28)

#define PHY_CTRL_R18						0x48
	#define PHY_CTRL_R18_MPLL_LKW_SEL			GENMASK(1, 0)
	#define PHY_CTRL_R18_MPLL_LK_W				GENMASK(5, 2)
	#define PHY_CTRL_R18_MPLL_LK_S				GENMASK(11, 6)
	#define PHY_CTRL_R18_MPLL_DCO_M_EN			BIT(12)
	#define PHY_CTRL_R18_MPLL_DCO_CLK_SEL			BIT(13)
	#define PHY_CTRL_R18_MPLL_PFD_GAIN			GENMASK(15, 14)
	#define PHY_CTRL_R18_MPLL_ROU				GENMASK(18, 16)
	#define PHY_CTRL_R18_MPLL_DATA_SEL			GENMASK(21, 19)
	#define PHY_CTRL_R18_MPLL_BIAS_ADJ			GENMASK(23, 22)
	#define PHY_CTRL_R18_MPLL_BB_MODE			GENMASK(25, 24)
	#define PHY_CTRL_R18_MPLL_ALPHA				GENMASK(28, 26)
	#define PHY_CTRL_R18_MPLL_ADJ_LDO			GENMASK(30, 29)
	#define PHY_CTRL_R18_MPLL_ACG_RANGE			BIT(31)

#define PHY_CTRL_R19						0x4c
#define PHY_CTRL_R20						0x50
	#define PHY_CTRL_R20_USB2_IDDET_EN			BIT(0)
	#define PHY_CTRL_R20_USB2_OTG_VBUS_TRIM_2_0		GENMASK(3, 1)
	#define PHY_CTRL_R20_USB2_OTG_VBUSDET_EN		BIT(4)
	#define PHY_CTRL_R20_USB2_AMON_EN			BIT(5)
	#define PHY_CTRL_R20_USB2_CAL_CODE_R5			BIT(6)
	#define PHY_CTRL_R20_BYPASS_OTG_DET			BIT(7)
	#define PHY_CTRL_R20_USB2_DMON_EN			BIT(8)
	#define PHY_CTRL_R20_USB2_DMON_SEL_3_0			GENMASK(12, 9)
	#define PHY_CTRL_R20_USB2_EDGE_DRV_EN			BIT(13)
	#define PHY_CTRL_R20_USB2_EDGE_DRV_TRIM_1_0		GENMASK(15, 14)
	#define PHY_CTRL_R20_USB2_BGR_ADJ_4_0			GENMASK(20, 16)
	#define PHY_CTRL_R20_USB2_BGR_START			BIT(21)
	#define PHY_CTRL_R20_USB2_BGR_VREF_4_0			GENMASK(28, 24)
	#define PHY_CTRL_R20_USB2_BGR_DBG_1_0			GENMASK(30, 29)
	#define PHY_CTRL_R20_BYPASS_CAL_DONE_R5			BIT(31)

#define PHY_CTRL_R21						0x54
	#define PHY_CTRL_R21_USB2_BGR_FORCE			BIT(0)
	#define PHY_CTRL_R21_USB2_CAL_ACK_EN			BIT(1)
	#define PHY_CTRL_R21_USB2_OTG_ACA_EN			BIT(2)
	#define PHY_CTRL_R21_USB2_TX_STRG_PD			BIT(3)
	#define PHY_CTRL_R21_USB2_OTG_ACA_TRIM_1_0		GENMASK(5, 4)
	#define PHY_CTRL_R21_BYPASS_UTMI_CNTR			GENMASK(15, 6)
	#define PHY_CTRL_R21_BYPASS_UTMI_REG			GENMASK(25, 20)

#define PHY_CTRL_R22						0x58
#define PHY_CTRL_R23						0x5c

#define RESET_COMPLETE_TIME					1000
#define PLL_RESET_COMPLETE_TIME					100

struct phy_meson_g12a_usb2_priv {
	struct device		*dev;
	struct regmap		*regmap;
	struct clk		*clk;
	struct reset_control	*reset;
};

static const struct regmap_config phy_meson_g12a_usb2_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = PHY_CTRL_R23,
};

static int phy_meson_g12a_usb2_init(struct phy *phy)
{
	struct phy_meson_g12a_usb2_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = reset_control_reset(priv->reset);
	if (ret)
		return ret;

	udelay(RESET_COMPLETE_TIME);

	/* usb2_otg_aca_en == 0 */
	regmap_update_bits(priv->regmap, PHY_CTRL_R21,
			   PHY_CTRL_R21_USB2_OTG_ACA_EN, 0);

	/* PLL Setup : 24MHz * 20 / 1 = 480MHz */
	regmap_write(priv->regmap, PHY_CTRL_R16,
		     FIELD_PREP(PHY_CTRL_R16_MPLL_M, 20) |
		     FIELD_PREP(PHY_CTRL_R16_MPLL_N, 1) |
		     PHY_CTRL_R16_MPLL_LOAD |
		     FIELD_PREP(PHY_CTRL_R16_MPLL_LOCK_LONG, 1) |
		     PHY_CTRL_R16_MPLL_FAST_LOCK |
		     PHY_CTRL_R16_MPLL_EN |
		     PHY_CTRL_R16_MPLL_RESET);

	regmap_write(priv->regmap, PHY_CTRL_R17,
		     FIELD_PREP(PHY_CTRL_R17_MPLL_FRAC_IN, 0) |
		     FIELD_PREP(PHY_CTRL_R17_MPLL_LAMBDA1, 7) |
		     FIELD_PREP(PHY_CTRL_R17_MPLL_LAMBDA0, 7) |
		     FIELD_PREP(PHY_CTRL_R17_MPLL_FILTER_PVT2, 2) |
		     FIELD_PREP(PHY_CTRL_R17_MPLL_FILTER_PVT1, 9));

	regmap_write(priv->regmap, PHY_CTRL_R18,
		     FIELD_PREP(PHY_CTRL_R18_MPLL_LKW_SEL, 1) |
		     FIELD_PREP(PHY_CTRL_R18_MPLL_LK_W, 9) |
		     FIELD_PREP(PHY_CTRL_R18_MPLL_LK_S, 0x27) |
		     FIELD_PREP(PHY_CTRL_R18_MPLL_PFD_GAIN, 1) |
		     FIELD_PREP(PHY_CTRL_R18_MPLL_ROU, 7) |
		     FIELD_PREP(PHY_CTRL_R18_MPLL_DATA_SEL, 3) |
		     FIELD_PREP(PHY_CTRL_R18_MPLL_BIAS_ADJ, 1) |
		     FIELD_PREP(PHY_CTRL_R18_MPLL_BB_MODE, 0) |
		     FIELD_PREP(PHY_CTRL_R18_MPLL_ALPHA, 3) |
		     FIELD_PREP(PHY_CTRL_R18_MPLL_ADJ_LDO, 1) |
		     PHY_CTRL_R18_MPLL_ACG_RANGE);

	udelay(PLL_RESET_COMPLETE_TIME);

	/* UnReset PLL */
	regmap_write(priv->regmap, PHY_CTRL_R16,
		     FIELD_PREP(PHY_CTRL_R16_MPLL_M, 20) |
		     FIELD_PREP(PHY_CTRL_R16_MPLL_N, 1) |
		     PHY_CTRL_R16_MPLL_LOAD |
		     FIELD_PREP(PHY_CTRL_R16_MPLL_LOCK_LONG, 1) |
		     PHY_CTRL_R16_MPLL_FAST_LOCK |
		     PHY_CTRL_R16_MPLL_EN);

	/* PHY Tuning */
	regmap_write(priv->regmap, PHY_CTRL_R20,
		     FIELD_PREP(PHY_CTRL_R20_USB2_OTG_VBUS_TRIM_2_0, 4) |
		     PHY_CTRL_R20_USB2_OTG_VBUSDET_EN |
		     FIELD_PREP(PHY_CTRL_R20_USB2_DMON_SEL_3_0, 15) |
		     PHY_CTRL_R20_USB2_EDGE_DRV_EN |
		     FIELD_PREP(PHY_CTRL_R20_USB2_EDGE_DRV_TRIM_1_0, 3) |
		     FIELD_PREP(PHY_CTRL_R20_USB2_BGR_ADJ_4_0, 0) |
		     FIELD_PREP(PHY_CTRL_R20_USB2_BGR_VREF_4_0, 0) |
		     FIELD_PREP(PHY_CTRL_R20_USB2_BGR_DBG_1_0, 0));

	regmap_write(priv->regmap, PHY_CTRL_R4,
		     FIELD_PREP(PHY_CTRL_R4_CALIB_CODE_7_0, 0xf) |
		     FIELD_PREP(PHY_CTRL_R4_CALIB_CODE_15_8, 0xf) |
		     FIELD_PREP(PHY_CTRL_R4_CALIB_CODE_23_16, 0xf) |
		     PHY_CTRL_R4_TEST_BYPASS_MODE_EN |
		     FIELD_PREP(PHY_CTRL_R4_I_C2L_BIAS_TRIM_1_0, 0) |
		     FIELD_PREP(PHY_CTRL_R4_I_C2L_BIAS_TRIM_3_2, 0));

	/* Tuning Disconnect Threshold */
	regmap_write(priv->regmap, PHY_CTRL_R3,
		     FIELD_PREP(PHY_CTRL_R3_SQUELCH_REF, 0) |
		     FIELD_PREP(PHY_CTRL_R3_HSDIC_REF, 1) |
		     FIELD_PREP(PHY_CTRL_R3_DISC_THRESH, 3));

	/* Analog Settings */
	regmap_write(priv->regmap, PHY_CTRL_R14, 0);
	regmap_write(priv->regmap, PHY_CTRL_R13,
		     PHY_CTRL_R13_UPDATE_PMA_SIGNALS |
		     FIELD_PREP(PHY_CTRL_R13_MIN_COUNT_FOR_SYNC_DET, 7));

	return 0;
}

static int phy_meson_g12a_usb2_exit(struct phy *phy)
{
	struct phy_meson_g12a_usb2_priv *priv = phy_get_drvdata(phy);

	return reset_control_reset(priv->reset);
}

/* set_mode is not needed, mode setting is handled via the UTMI bus */
static const struct phy_ops phy_meson_g12a_usb2_ops = {
	.init		= phy_meson_g12a_usb2_init,
	.exit		= phy_meson_g12a_usb2_exit,
	.owner		= THIS_MODULE,
};

static int phy_meson_g12a_usb2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	struct resource *res;
	struct phy_meson_g12a_usb2_priv *priv;
	struct phy *phy;
	void __iomem *base;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	platform_set_drvdata(pdev, priv);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->regmap = devm_regmap_init_mmio(dev, base,
					     &phy_meson_g12a_usb2_regmap_conf);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->clk = devm_clk_get(dev, "xtal");
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	priv->reset = devm_reset_control_get(dev, "phy");
	if (IS_ERR(priv->reset))
		return PTR_ERR(priv->reset);

	ret = reset_control_deassert(priv->reset);
	if (ret)
		return ret;

	phy = devm_phy_create(dev, NULL, &phy_meson_g12a_usb2_ops);
	if (IS_ERR(phy)) {
		ret = PTR_ERR(phy);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to create PHY\n");

		return ret;
	}

	phy_set_bus_width(phy, 8);
	phy_set_drvdata(phy, priv);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id phy_meson_g12a_usb2_of_match[] = {
	{ .compatible = "amlogic,g12a-usb2-phy", },
	{ },
};
MODULE_DEVICE_TABLE(of, phy_meson_g12a_usb2_of_match);

static struct platform_driver phy_meson_g12a_usb2_driver = {
	.probe	= phy_meson_g12a_usb2_probe,
	.driver	= {
		.name		= "phy-meson-g12a-usb2",
		.of_match_table	= phy_meson_g12a_usb2_of_match,
	},
};
module_platform_driver(phy_meson_g12a_usb2_driver);

MODULE_AUTHOR("Martin Blumenstingl <martin.blumenstingl@googlemail.com>");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_DESCRIPTION("Meson G12A USB2 PHY driver");
MODULE_LICENSE("GPL v2");
