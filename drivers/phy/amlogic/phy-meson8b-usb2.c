// SPDX-License-Identifier: GPL-2.0-only
/*
 * Meson8, Meson8b and GXBB USB2 PHY driver
 *
 * Copyright (C) 2016 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/usb/of.h>

#define REG_CONFIG					0x00
	#define REG_CONFIG_CLK_EN			BIT(0)
	#define REG_CONFIG_CLK_SEL_MASK			GENMASK(3, 1)
	#define REG_CONFIG_CLK_DIV_MASK			GENMASK(10, 4)
	#define REG_CONFIG_CLK_32k_ALTSEL		BIT(15)
	#define REG_CONFIG_TEST_TRIG			BIT(31)

#define REG_CTRL					0x04
	#define REG_CTRL_SOFT_PRST			BIT(0)
	#define REG_CTRL_SOFT_HRESET			BIT(1)
	#define REG_CTRL_SS_SCALEDOWN_MODE_MASK		GENMASK(3, 2)
	#define REG_CTRL_CLK_DET_RST			BIT(4)
	#define REG_CTRL_INTR_SEL			BIT(5)
	#define REG_CTRL_CLK_DETECTED			BIT(8)
	#define REG_CTRL_SOF_SENT_RCVD_TGL		BIT(9)
	#define REG_CTRL_SOF_TOGGLE_OUT			BIT(10)
	#define REG_CTRL_POWER_ON_RESET			BIT(15)
	#define REG_CTRL_SLEEPM				BIT(16)
	#define REG_CTRL_TX_BITSTUFF_ENN_H		BIT(17)
	#define REG_CTRL_TX_BITSTUFF_ENN		BIT(18)
	#define REG_CTRL_COMMON_ON			BIT(19)
	#define REG_CTRL_REF_CLK_SEL_MASK		GENMASK(21, 20)
	#define REG_CTRL_REF_CLK_SEL_SHIFT		20
	#define REG_CTRL_FSEL_MASK			GENMASK(24, 22)
	#define REG_CTRL_FSEL_SHIFT			22
	#define REG_CTRL_PORT_RESET			BIT(25)
	#define REG_CTRL_THREAD_ID_MASK			GENMASK(31, 26)

#define REG_ENDP_INTR					0x08

/* bits [31:26], [24:21] and [15:3] seem to be read-only */
#define REG_ADP_BC					0x0c
	#define REG_ADP_BC_VBUS_VLD_EXT_SEL		BIT(0)
	#define REG_ADP_BC_VBUS_VLD_EXT			BIT(1)
	#define REG_ADP_BC_OTG_DISABLE			BIT(2)
	#define REG_ADP_BC_ID_PULLUP			BIT(3)
	#define REG_ADP_BC_DRV_VBUS			BIT(4)
	#define REG_ADP_BC_ADP_PRB_EN			BIT(5)
	#define REG_ADP_BC_ADP_DISCHARGE		BIT(6)
	#define REG_ADP_BC_ADP_CHARGE			BIT(7)
	#define REG_ADP_BC_SESS_END			BIT(8)
	#define REG_ADP_BC_DEVICE_SESS_VLD		BIT(9)
	#define REG_ADP_BC_B_VALID			BIT(10)
	#define REG_ADP_BC_A_VALID			BIT(11)
	#define REG_ADP_BC_ID_DIG			BIT(12)
	#define REG_ADP_BC_VBUS_VALID			BIT(13)
	#define REG_ADP_BC_ADP_PROBE			BIT(14)
	#define REG_ADP_BC_ADP_SENSE			BIT(15)
	#define REG_ADP_BC_ACA_ENABLE			BIT(16)
	#define REG_ADP_BC_DCD_ENABLE			BIT(17)
	#define REG_ADP_BC_VDAT_DET_EN_B		BIT(18)
	#define REG_ADP_BC_VDAT_SRC_EN_B		BIT(19)
	#define REG_ADP_BC_CHARGE_SEL			BIT(20)
	#define REG_ADP_BC_CHARGE_DETECT		BIT(21)
	#define REG_ADP_BC_ACA_PIN_RANGE_C		BIT(22)
	#define REG_ADP_BC_ACA_PIN_RANGE_B		BIT(23)
	#define REG_ADP_BC_ACA_PIN_RANGE_A		BIT(24)
	#define REG_ADP_BC_ACA_PIN_GND			BIT(25)
	#define REG_ADP_BC_ACA_PIN_FLOAT		BIT(26)

#define REG_DBG_UART					0x10

#define REG_TEST					0x14
	#define REG_TEST_DATA_IN_MASK			GENMASK(3, 0)
	#define REG_TEST_EN_MASK			GENMASK(7, 4)
	#define REG_TEST_ADDR_MASK			GENMASK(11, 8)
	#define REG_TEST_DATA_OUT_SEL			BIT(12)
	#define REG_TEST_CLK				BIT(13)
	#define REG_TEST_VA_TEST_EN_B_MASK		GENMASK(15, 14)
	#define REG_TEST_DATA_OUT_MASK			GENMASK(19, 16)
	#define REG_TEST_DISABLE_ID_PULLUP		BIT(20)

#define REG_TUNE					0x18
	#define REG_TUNE_TX_RES_TUNE_MASK		GENMASK(1, 0)
	#define REG_TUNE_TX_HSXV_TUNE_MASK		GENMASK(3, 2)
	#define REG_TUNE_TX_VREF_TUNE_MASK		GENMASK(7, 4)
	#define REG_TUNE_TX_RISE_TUNE_MASK		GENMASK(9, 8)
	#define REG_TUNE_TX_PREEMP_PULSE_TUNE		BIT(10)
	#define REG_TUNE_TX_PREEMP_AMP_TUNE_MASK	GENMASK(12, 11)
	#define REG_TUNE_TX_FSLS_TUNE_MASK		GENMASK(16, 13)
	#define REG_TUNE_SQRX_TUNE_MASK			GENMASK(19, 17)
	#define REG_TUNE_OTG_TUNE			GENMASK(22, 20)
	#define REG_TUNE_COMP_DIS_TUNE			GENMASK(25, 23)
	#define REG_TUNE_HOST_DM_PULLDOWN		BIT(26)
	#define REG_TUNE_HOST_DP_PULLDOWN		BIT(27)

#define RESET_COMPLETE_TIME				500
#define ACA_ENABLE_COMPLETE_TIME			50

struct phy_meson8b_usb2_priv {
	struct regmap		*regmap;
	enum usb_dr_mode	dr_mode;
	struct clk		*clk_usb_general;
	struct clk		*clk_usb;
	struct reset_control	*reset;
};

static const struct regmap_config phy_meson8b_usb2_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = REG_TUNE,
};

static int phy_meson8b_usb2_power_on(struct phy *phy)
{
	struct phy_meson8b_usb2_priv *priv = phy_get_drvdata(phy);
	u32 reg;
	int ret;

	if (!IS_ERR_OR_NULL(priv->reset)) {
		ret = reset_control_reset(priv->reset);
		if (ret) {
			dev_err(&phy->dev, "Failed to trigger USB reset\n");
			return ret;
		}
	}

	ret = clk_prepare_enable(priv->clk_usb_general);
	if (ret) {
		dev_err(&phy->dev, "Failed to enable USB general clock\n");
		return ret;
	}

	ret = clk_prepare_enable(priv->clk_usb);
	if (ret) {
		dev_err(&phy->dev, "Failed to enable USB DDR clock\n");
		clk_disable_unprepare(priv->clk_usb_general);
		return ret;
	}

	regmap_update_bits(priv->regmap, REG_CONFIG, REG_CONFIG_CLK_32k_ALTSEL,
			   REG_CONFIG_CLK_32k_ALTSEL);

	regmap_update_bits(priv->regmap, REG_CTRL, REG_CTRL_REF_CLK_SEL_MASK,
			   0x2 << REG_CTRL_REF_CLK_SEL_SHIFT);

	regmap_update_bits(priv->regmap, REG_CTRL, REG_CTRL_FSEL_MASK,
			   0x5 << REG_CTRL_FSEL_SHIFT);

	/* reset the PHY */
	regmap_update_bits(priv->regmap, REG_CTRL, REG_CTRL_POWER_ON_RESET,
			   REG_CTRL_POWER_ON_RESET);
	udelay(RESET_COMPLETE_TIME);
	regmap_update_bits(priv->regmap, REG_CTRL, REG_CTRL_POWER_ON_RESET, 0);
	udelay(RESET_COMPLETE_TIME);

	regmap_update_bits(priv->regmap, REG_CTRL, REG_CTRL_SOF_TOGGLE_OUT,
			   REG_CTRL_SOF_TOGGLE_OUT);

	if (priv->dr_mode == USB_DR_MODE_HOST) {
		regmap_update_bits(priv->regmap, REG_ADP_BC,
				   REG_ADP_BC_ACA_ENABLE,
				   REG_ADP_BC_ACA_ENABLE);

		udelay(ACA_ENABLE_COMPLETE_TIME);

		regmap_read(priv->regmap, REG_ADP_BC, &reg);
		if (reg & REG_ADP_BC_ACA_PIN_FLOAT) {
			dev_warn(&phy->dev, "USB ID detect failed!\n");
			clk_disable_unprepare(priv->clk_usb);
			clk_disable_unprepare(priv->clk_usb_general);
			return -EINVAL;
		}
	}

	return 0;
}

static int phy_meson8b_usb2_power_off(struct phy *phy)
{
	struct phy_meson8b_usb2_priv *priv = phy_get_drvdata(phy);

	clk_disable_unprepare(priv->clk_usb);
	clk_disable_unprepare(priv->clk_usb_general);

	return 0;
}

static const struct phy_ops phy_meson8b_usb2_ops = {
	.power_on	= phy_meson8b_usb2_power_on,
	.power_off	= phy_meson8b_usb2_power_off,
	.owner		= THIS_MODULE,
};

static int phy_meson8b_usb2_probe(struct platform_device *pdev)
{
	struct phy_meson8b_usb2_priv *priv;
	struct phy *phy;
	struct phy_provider *phy_provider;
	void __iomem *base;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	priv->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					     &phy_meson8b_usb2_regmap_conf);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->clk_usb_general = devm_clk_get(&pdev->dev, "usb_general");
	if (IS_ERR(priv->clk_usb_general))
		return PTR_ERR(priv->clk_usb_general);

	priv->clk_usb = devm_clk_get(&pdev->dev, "usb");
	if (IS_ERR(priv->clk_usb))
		return PTR_ERR(priv->clk_usb);

	priv->reset = devm_reset_control_get_optional_shared(&pdev->dev, NULL);
	if (PTR_ERR(priv->reset) == -EPROBE_DEFER)
		return PTR_ERR(priv->reset);

	priv->dr_mode = of_usb_get_dr_mode_by_phy(pdev->dev.of_node, -1);
	if (priv->dr_mode == USB_DR_MODE_UNKNOWN) {
		dev_err(&pdev->dev,
			"missing dual role configuration of the controller\n");
		return -EINVAL;
	}

	phy = devm_phy_create(&pdev->dev, NULL, &phy_meson8b_usb2_ops);
	if (IS_ERR(phy)) {
		dev_err(&pdev->dev, "failed to create PHY\n");
		return PTR_ERR(phy);
	}

	phy_set_drvdata(phy, priv);

	phy_provider =
		devm_of_phy_provider_register(&pdev->dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id phy_meson8b_usb2_of_match[] = {
	{ .compatible = "amlogic,meson8-usb2-phy", },
	{ .compatible = "amlogic,meson8b-usb2-phy", },
	{ .compatible = "amlogic,meson-gxbb-usb2-phy", },
	{ },
};
MODULE_DEVICE_TABLE(of, phy_meson8b_usb2_of_match);

static struct platform_driver phy_meson8b_usb2_driver = {
	.probe	= phy_meson8b_usb2_probe,
	.driver	= {
		.name		= "phy-meson-usb2",
		.of_match_table	= phy_meson8b_usb2_of_match,
	},
};
module_platform_driver(phy_meson8b_usb2_driver);

MODULE_AUTHOR("Martin Blumenstingl <martin.blumenstingl@googlemail.com>");
MODULE_DESCRIPTION("Meson8, Meson8b and GXBB USB2 PHY driver");
MODULE_LICENSE("GPL");
