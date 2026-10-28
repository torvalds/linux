// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 Aspeed Technology Inc.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#define PHY3S00				0x00
#define  PHY3S00_INIT_DONE		BIT(15)
#define  PHY3S00_SRAM_BYPASS		BIT(7)
#define  PHY3S00_SRAM_EXT_LOAD		BIT(6)
#define PHY3S04				0x04
#define PHY3C00				0x08
#define PHY3C04				0x0c
#define PHY3P00				0x10
#define  PHY3P00_RX_ADAPT_AFE_EN_G1	BIT(0)
#define  PHY3P00_RX_ADAPT_AFE_EN_G2	BIT(1)
#define  PHY3P00_RX_ADAPT_DFE_EN_G1	BIT(2)
#define  PHY3P00_RX_ADAPT_DFE_EN_G2	BIT(3)
#define  PHY3P00_RX_CDR_VCO_LOWFREQ_G1	BIT(4)
#define  PHY3P00_RX_CDR_VCO_LOWFREQ_G2	BIT(5)
#define  PHY3P00_RX_EQ_AFE_GAIN_G1	GENMASK(9, 6)
#define  PHY3P00_RX_EQ_AFE_GAIN_G2	GENMASK(13, 10)
#define  PHY3P00_RX_EQ_ATT_LVL_G1	GENMASK(16, 14)
#define  PHY3P00_RX_EQ_ATT_LVL_G2	GENMASK(19, 17)
#define  PHY3P00_RX_EQ_CTLE_BOOST_G1	GENMASK(24, 20)
#define  PHY3P00_RX_EQ_CTLE_BOOST_G2	GENMASK(29, 25)
#define  PHY3P00_RX_EQ_DELTA_IQ_G1_LO	GENMASK(31, 30)

#define PHY3P04				0x14
#define  PHY3P04_RX_EQ_DELTA_IQ_G1_HI	GENMASK(1, 0)
#define  PHY3P04_RX_EQ_DELTA_IQ_G2	GENMASK(5, 2)
#define  PHY3P04_RX_EQ_DFE_TAP1_G1	GENMASK(13, 6)
#define  PHY3P04_RX_EQ_DFE_TAP1_G2	GENMASK(21, 14)
#define  PHY3P04_RX_LOS_LFPS_EN		BIT(22)
#define  PHY3P04_RX_LOS_THRESHOLD	GENMASK(25, 23)
#define  PHY3P04_RX_TERM_CTRL		GENMASK(28, 26)
#define  PHY3P04_TX_EQ_MAIN_G1_LO	GENMASK(31, 29)

#define PHY3P08				0x18
#define  PHY3P08_TX_EQ_MAIN_G1_HI	GENMASK(1, 0)
#define  PHY3P08_TX_EQ_MAIN_G2		GENMASK(6, 2)
#define  PHY3P08_TX_EQ_OVRD		BIT(7)
#define  PHY3P08_TX_EQ_POST_G1		GENMASK(12, 9)
#define  PHY3P08_TX_EQ_POST_G2		GENMASK(16, 13)
#define  PHY3P08_TX_EQ_PRE_G1		GENMASK(20, 17)
#define  PHY3P08_TX_EQ_PRE_G2		GENMASK(24, 21)
#define  PHY3P08_TX_IBOOST_LVL		GENMASK(28, 25)
#define  PHY3P08_TX_TERM_CTRL		GENMASK(31, 29)

#define PHY3P0C				0x1c
#define  PHY3P0C_TX_VBOOST_EN		BIT(0)

#define PHY3CMD				0x40

#define PHY3P_RX_EQ_CTLE_BOOST_G1_DEFAULT	0x7
#define PHY3P_RX_EQ_CTLE_BOOST_G2_DEFAULT	0x7
#define PHY3P_RX_EQ_DELTA_IQ_G1_DEFAULT		0x3
#define PHY3P_RX_EQ_DELTA_IQ_G2_DEFAULT		0x5
#define PHY3P_RX_LOS_THRESHOLD_DEFAULT		0x3
#define PHY3P_RX_TERM_CTRL_DEFAULT		0x2
#define PHY3P_TX_EQ_MAIN_G1_DEFAULT		0xa
#define PHY3P_TX_EQ_MAIN_G2_DEFAULT		0x9
#define PHY3P_TX_EQ_POST_G1_DEFAULT		0x4
#define PHY3P_TX_EQ_POST_G2_DEFAULT		0x3
#define PHY3P_TX_EQ_PRE_G2_DEFAULT		0x2
#define PHY3P_TX_IBOOST_LVL_DEFAULT		0xf
#define PHY3P_TX_TERM_CTRL_DEFAULT		0x2

#define PHY3P00_DEFAULT ( \
	PHY3P00_RX_ADAPT_AFE_EN_G1 | \
	PHY3P00_RX_ADAPT_AFE_EN_G2 | \
	PHY3P00_RX_ADAPT_DFE_EN_G1 | \
	PHY3P00_RX_ADAPT_DFE_EN_G2 | \
	FIELD_PREP(PHY3P00_RX_EQ_CTLE_BOOST_G1, PHY3P_RX_EQ_CTLE_BOOST_G1_DEFAULT) | \
	FIELD_PREP(PHY3P00_RX_EQ_CTLE_BOOST_G2, PHY3P_RX_EQ_CTLE_BOOST_G2_DEFAULT) | \
	FIELD_PREP(PHY3P00_RX_EQ_DELTA_IQ_G1_LO, \
		   PHY3P_RX_EQ_DELTA_IQ_G1_DEFAULT & 0x3) \
)

#define PHY3P04_DEFAULT ( \
	FIELD_PREP(PHY3P04_RX_EQ_DELTA_IQ_G1_HI, \
		   PHY3P_RX_EQ_DELTA_IQ_G1_DEFAULT >> 2) | \
	FIELD_PREP(PHY3P04_RX_EQ_DELTA_IQ_G2, PHY3P_RX_EQ_DELTA_IQ_G2_DEFAULT) | \
	PHY3P04_RX_LOS_LFPS_EN | \
	FIELD_PREP(PHY3P04_RX_LOS_THRESHOLD, PHY3P_RX_LOS_THRESHOLD_DEFAULT) | \
	FIELD_PREP(PHY3P04_RX_TERM_CTRL, PHY3P_RX_TERM_CTRL_DEFAULT) | \
	FIELD_PREP(PHY3P04_TX_EQ_MAIN_G1_LO, \
		   PHY3P_TX_EQ_MAIN_G1_DEFAULT & 0x7) \
)

#define PHY3P08_DEFAULT ( \
	FIELD_PREP(PHY3P08_TX_EQ_MAIN_G1_HI, PHY3P_TX_EQ_MAIN_G1_DEFAULT >> 3) | \
	FIELD_PREP(PHY3P08_TX_EQ_MAIN_G2, PHY3P_TX_EQ_MAIN_G2_DEFAULT) | \
	FIELD_PREP(PHY3P08_TX_EQ_POST_G1, PHY3P_TX_EQ_POST_G1_DEFAULT) | \
	FIELD_PREP(PHY3P08_TX_EQ_POST_G2, PHY3P_TX_EQ_POST_G2_DEFAULT) | \
	FIELD_PREP(PHY3P08_TX_EQ_PRE_G2, PHY3P_TX_EQ_PRE_G2_DEFAULT) | \
	FIELD_PREP(PHY3P08_TX_IBOOST_LVL, PHY3P_TX_IBOOST_LVL_DEFAULT) | \
	FIELD_PREP(PHY3P08_TX_TERM_CTRL, PHY3P_TX_TERM_CTRL_DEFAULT) \
)

#define PHY3P0C_DEFAULT \
	PHY3P0C_TX_VBOOST_EN

struct aspeed_usb3_phy {
	void __iomem *regs;
	struct reset_control *rst;
	struct device *dev;
	struct clk *clk;
};

static int aspeed_usb3_phy_init(struct phy *phy)
{
	struct aspeed_usb3_phy *aspeed_phy = phy_get_drvdata(phy);
	u32 val;
	int ret;

	ret = clk_prepare_enable(aspeed_phy->clk);
	if (ret) {
		dev_err(aspeed_phy->dev, "Failed to enable clock %d\n", ret);
		return ret;
	}

	ret = reset_control_deassert(aspeed_phy->rst);
	if (ret)
		goto err_disable_clk;

	/* Wait for USB3 PHY internal SRAM initialization done */
	ret = readl_poll_timeout(aspeed_phy->regs + PHY3S00, val,
				 val & PHY3S00_INIT_DONE,
				 USEC_PER_MSEC, 10 * USEC_PER_MSEC);
	if (ret) {
		dev_err(aspeed_phy->dev, "SRAM init timeout\n");
		goto err_assert_reset;
	}

	val = readl(aspeed_phy->regs + PHY3S00);
	val |= PHY3S00_SRAM_EXT_LOAD;
	writel(val, aspeed_phy->regs + PHY3S00);

	/* Set protocol1_ext signals as default PHY3 settings based on SNPS documents.
	 * Including PCFGI[54]: protocol1_ext_rx_los_lfps_en for better compatibility
	 */
	writel(PHY3P00_DEFAULT, aspeed_phy->regs + PHY3P00);
	writel(PHY3P04_DEFAULT, aspeed_phy->regs + PHY3P04);
	writel(PHY3P08_DEFAULT, aspeed_phy->regs + PHY3P08);
	writel(PHY3P0C_DEFAULT, aspeed_phy->regs + PHY3P0C);

	return 0;

err_assert_reset:
	reset_control_assert(aspeed_phy->rst);
err_disable_clk:
	clk_disable_unprepare(aspeed_phy->clk);
	return ret;
}

static int aspeed_usb3_phy_exit(struct phy *phy)
{
	struct aspeed_usb3_phy *aspeed_phy = phy_get_drvdata(phy);

	reset_control_assert(aspeed_phy->rst);
	clk_disable_unprepare(aspeed_phy->clk);

	return 0;
}

static const struct phy_ops aspeed_usb3_phy_ops = {
	.init		= aspeed_usb3_phy_init,
	.exit		= aspeed_usb3_phy_exit,
	.owner		= THIS_MODULE,
};

static int aspeed_usb3_phy_probe(struct platform_device *pdev)
{
	struct aspeed_usb3_phy *aspeed_phy;
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct phy *phy;

	aspeed_phy = devm_kzalloc(dev, sizeof(*aspeed_phy), GFP_KERNEL);
	if (!aspeed_phy)
		return -ENOMEM;

	aspeed_phy->dev = dev;

	aspeed_phy->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(aspeed_phy->clk))
		return PTR_ERR(aspeed_phy->clk);

	aspeed_phy->rst = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(aspeed_phy->rst))
		return PTR_ERR(aspeed_phy->rst);

	aspeed_phy->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(aspeed_phy->regs))
		return PTR_ERR(aspeed_phy->regs);

	phy = devm_phy_create(dev, NULL, &aspeed_usb3_phy_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	phy_set_drvdata(phy, aspeed_phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id aspeed_usb3_phy_match_table[] = {
	{
		.compatible = "aspeed,ast2700-usb3-phy",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, aspeed_usb3_phy_match_table);

static struct platform_driver aspeed_usb3_phy_driver = {
	.probe		= aspeed_usb3_phy_probe,
	.driver		= {
		.name	= KBUILD_MODNAME,
		.of_match_table	= aspeed_usb3_phy_match_table,
	},
};
module_platform_driver(aspeed_usb3_phy_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ASPEED USB3.2 PHY Driver");
MODULE_AUTHOR("Ryan Chen <ryan_chen@aspeedtech.com>");
