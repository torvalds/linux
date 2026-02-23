// SPDX-License-Identifier: GPL-2.0-only
/*
 * SpacemiT K1 USB 2.0 PHY driver
 *
 * Copyright (C) 2025 SpacemiT (Hangzhou) Technology Co. Ltd
 * Copyright (C) 2025 Ze Huang <huang.ze@linux.dev>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/usb/of.h>

#define PHY_RST_MODE_CTRL		0x04
#define  PHY_PLL_RDY			BIT(0)
#define  PHY_CLK_CDR_EN			BIT(1)
#define  PHY_CLK_PLL_EN			BIT(2)
#define  PHY_CLK_MAC_EN			BIT(3)
#define  PHY_MAC_RSTN			BIT(5)
#define  PHY_CDR_RSTN			BIT(6)
#define  PHY_PLL_RSTN			BIT(7)
/*
 * hs line state sel (Bit 13):
 * - 1 (Default): Internal HS line state is set to 01 when usb_hs_tx_en is valid.
 * - 0: Internal HS line state is always driven by usb_hs_lstate.
 *
 * fs line state sel (Bit 14):
 * - 1 (Default): FS line state is determined by the output data
 * (usb_fs_datain/b).
 * - 0: FS line state is always determined by the input data (dmo/dpo).
 */
#define  PHY_HS_LINE_TX_MODE		BIT(13)
#define  PHY_FS_LINE_TX_MODE		BIT(14)

#define  PHY_INIT_MODE_BITS		(PHY_FS_LINE_TX_MODE | PHY_HS_LINE_TX_MODE)
#define  PHY_CLK_ENABLE_BITS		(PHY_CLK_PLL_EN | PHY_CLK_CDR_EN | \
					 PHY_CLK_MAC_EN)
#define  PHY_DEASSERT_RST_BITS		(PHY_PLL_RSTN | PHY_CDR_RSTN | \
					 PHY_MAC_RSTN)

#define PHY_TX_HOST_CTRL		0x10
#define  PHY_HST_DISC_AUTO_CLR		BIT(2)		/* autoclear hs host disc when re-connect */

#define PHY_HSTXP_HW_CTRL		0x34
#define  PHY_HSTXP_RSTN			BIT(2)		/* generate reset for clock hstxp */
#define  PHY_CLK_HSTXP_EN		BIT(3)		/* clock hstxp enable */
#define  PHY_HSTXP_MODE			BIT(4)		/* 0: force en_txp to be 1; 1: no force */

#define PHY_PLL_DIV_CFG			0x98
#define  PHY_FDIV_FRACT_8_15		GENMASK(7, 0)
#define  PHY_FDIV_FRACT_16_19		GENMASK(11, 8)
#define  PHY_FDIV_FRACT_20_21		BIT(12)		/* fdiv_reg<21>, <20>, bit21 == bit20 */
/*
 * freq_sel<1:0>
 * if ref clk freq=24.0MHz-->freq_sel<2:0> == 3b'001, then internal divider value == 80
 */
#define  PHY_FDIV_FRACT_0_1		GENMASK(14, 13)
/*
 * pll divider value selection
 * 1: divider value will choose internal default value ,dependent on freq_sel<1:0>
 * 0: divider value will be over ride by fdiv_reg<21:0>
 */
#define  PHY_DIV_LOCAL_EN		BIT(15)

#define  PHY_SEL_FREQ_24MHZ		0x01
#define  FDIV_REG_MASK			(PHY_FDIV_FRACT_20_21 | PHY_FDIV_FRACT_16_19 | \
					 PHY_FDIV_FRACT_8_15)
#define  FDIV_REG_VAL			0x1ec4		/* 0x100 selects 24MHz, rest are default */

#define K1_USB2PHY_RESET_TIME_MS	50

struct spacemit_usb2phy {
	struct phy *phy;
	struct clk *clk;
	struct regmap *regmap_base;
};

static const struct regmap_config phy_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x200,
};

static int spacemit_usb2phy_init(struct phy *phy)
{
	struct spacemit_usb2phy *sphy = phy_get_drvdata(phy);
	struct regmap *map = sphy->regmap_base;
	u32 val;
	int ret;

	ret = clk_enable(sphy->clk);
	if (ret) {
		dev_err(&phy->dev, "failed to enable clock\n");
		clk_disable(sphy->clk);
		return ret;
	}

	/*
	 * make sure the usb controller is not under reset process before
	 * any configuration
	 */
	usleep_range(150, 200);

	/* 24M ref clk */
	val = FIELD_PREP(FDIV_REG_MASK, FDIV_REG_VAL) |
	      FIELD_PREP(PHY_FDIV_FRACT_0_1, PHY_SEL_FREQ_24MHZ) |
	      PHY_DIV_LOCAL_EN;
	regmap_write(map, PHY_PLL_DIV_CFG, val);

	ret = regmap_read_poll_timeout(map, PHY_RST_MODE_CTRL, val,
				       (val & PHY_PLL_RDY),
				       500, K1_USB2PHY_RESET_TIME_MS * 1000);
	if (ret) {
		dev_err(&phy->dev, "wait PLLREADY timeout\n");
		clk_disable(sphy->clk);
		return ret;
	}

	/* release usb2 phy internal reset and enable clock gating */
	val = (PHY_INIT_MODE_BITS | PHY_CLK_ENABLE_BITS | PHY_DEASSERT_RST_BITS);
	regmap_write(map, PHY_RST_MODE_CTRL, val);

	val = (PHY_HSTXP_RSTN | PHY_CLK_HSTXP_EN | PHY_HSTXP_MODE);
	regmap_write(map, PHY_HSTXP_HW_CTRL, val);

	/* auto clear host disc */
	regmap_update_bits(map, PHY_TX_HOST_CTRL, PHY_HST_DISC_AUTO_CLR,
			   PHY_HST_DISC_AUTO_CLR);

	return 0;
}

static int spacemit_usb2phy_exit(struct phy *phy)
{
	struct spacemit_usb2phy *sphy = phy_get_drvdata(phy);

	clk_disable(sphy->clk);

	return 0;
}

static const struct phy_ops spacemit_usb2phy_ops = {
	.init = spacemit_usb2phy_init,
	.exit = spacemit_usb2phy_exit,
	.owner = THIS_MODULE,
};

static int spacemit_usb2phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct spacemit_usb2phy *sphy;
	void __iomem *base;

	sphy = devm_kzalloc(dev, sizeof(*sphy), GFP_KERNEL);
	if (!sphy)
		return -ENOMEM;

	sphy->clk = devm_clk_get_prepared(&pdev->dev, NULL);
	if (IS_ERR(sphy->clk))
		return dev_err_probe(dev, PTR_ERR(sphy->clk), "Failed to get clock\n");

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	sphy->regmap_base = devm_regmap_init_mmio(dev, base, &phy_regmap_config);
	if (IS_ERR(sphy->regmap_base))
		return dev_err_probe(dev, PTR_ERR(sphy->regmap_base), "Failed to init regmap\n");

	sphy->phy = devm_phy_create(dev, NULL, &spacemit_usb2phy_ops);
	if (IS_ERR(sphy->phy))
		return dev_err_probe(dev, PTR_ERR(sphy->phy), "Failed to create phy\n");

	phy_set_drvdata(sphy->phy, sphy);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id spacemit_usb2phy_dt_match[] = {
	{ .compatible = "spacemit,k1-usb2-phy", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, spacemit_usb2phy_dt_match);

static struct platform_driver spacemit_usb2_phy_driver = {
	.probe	= spacemit_usb2phy_probe,
	.driver = {
		.name   = "spacemit-usb2-phy",
		.of_match_table = spacemit_usb2phy_dt_match,
	},
};
module_platform_driver(spacemit_usb2_phy_driver);

MODULE_DESCRIPTION("Spacemit USB 2.0 PHY driver");
MODULE_LICENSE("GPL");
