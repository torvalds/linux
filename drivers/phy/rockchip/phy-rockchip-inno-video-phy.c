// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 *
 * Author: Wyon Bi <bivvy.bi@rock-chips.com>
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/pm_runtime.h>

/* 0x0030 */
#define DISABLE_PLL	BIT(3)
/* 0x003c */
#define PLL_LOCK	BIT(1)
/* 0x0084 */
#define ENABLE_TX	BIT(7)

struct inno_video_phy {
	struct device *dev;
	struct clk *pclk;
	struct regmap *regmap;
	struct reset_control *rst;
};

static const struct reg_sequence ttl_mode[] = {
	{ 0x0000, 0x7f },
	{ 0x0004, 0x3f },
	{ 0x0008, 0x80 },
	{ 0x0010, 0x3f },
	{ 0x0014, 0x3f },
	{ 0x0080, 0x44 },

	{ 0x0100, 0x7f },
	{ 0x0104, 0x3f },
	{ 0x0108, 0x80 },
	{ 0x0110, 0x3f },
	{ 0x0114, 0x3f },
	{ 0x0180, 0x44 },
};

static const struct reg_sequence lvds_mode_single_channel[] = {
	{ 0x0000, 0xbf },
	{ 0x0004, 0x3f },
	{ 0x0008, 0xfe },
	{ 0x0010, 0x00 },
	{ 0x0014, 0x00 },
	{ 0x0080, 0x44 },

	{ 0x0100, 0x00 },
	{ 0x0104, 0x00 },
	{ 0x0108, 0x00 },
	{ 0x0110, 0x00 },
	{ 0x0114, 0x00 },
	{ 0x0180, 0x44 },
};

static const struct reg_sequence lvds_mode_dual_channel[] = {
	{ 0x0000, 0xbf },
	{ 0x0004, 0x3f },
	{ 0x0008, 0xfe },
	{ 0x0010, 0x00 },
	{ 0x0014, 0x00 },
	{ 0x0080, 0x44 },

	{ 0x0100, 0xbf },
	{ 0x0104, 0x3f },
	{ 0x0108, 0xfe },
	{ 0x0110, 0x00 },
	{ 0x0114, 0x00 },
	{ 0x0180, 0x44 },
};

static int inno_video_phy_power_on(struct phy *phy)
{
	struct inno_video_phy *inno = phy_get_drvdata(phy);
	enum phy_mode mode = phy_get_mode(phy);
	const struct reg_sequence *wseq;
	bool dual_channel = phy_get_bus_width(phy) == 2 ? true : false;
	int nregs;
	u32 status;
	int ret;

	clk_prepare_enable(inno->pclk);
	pm_runtime_get_sync(inno->dev);

	switch (mode) {
	case PHY_MODE_LVDS:
		if (dual_channel) {
			wseq = lvds_mode_dual_channel;
			nregs = ARRAY_SIZE(lvds_mode_dual_channel);
		} else {
			wseq = lvds_mode_single_channel;
			nregs = ARRAY_SIZE(lvds_mode_single_channel);
		}
		break;
	default:
		wseq = ttl_mode;
		nregs = ARRAY_SIZE(ttl_mode);
		break;
	}

	regmap_multi_reg_write(inno->regmap, wseq, nregs);

	regmap_update_bits(inno->regmap, 0x0030, DISABLE_PLL, 0);
	ret = regmap_read_poll_timeout(inno->regmap, 0x003c, status,
				       status & PLL_LOCK, 50, 5000);
	if (ret) {
		dev_err(inno->dev, "PLL is not lock\n");
		return ret;
	}

	regmap_update_bits(inno->regmap, 0x0084, ENABLE_TX, ENABLE_TX);

	return 0;
}

static int inno_video_phy_power_off(struct phy *phy)
{
	struct inno_video_phy *inno = phy_get_drvdata(phy);

	regmap_update_bits(inno->regmap, 0x0084, ENABLE_TX, 0);
	regmap_update_bits(inno->regmap, 0x0030, DISABLE_PLL, DISABLE_PLL);

	pm_runtime_put(inno->dev);
	clk_disable_unprepare(inno->pclk);

	return 0;
}

static int inno_video_phy_set_mode(struct phy *phy, enum phy_mode mode)
{
	return 0;
}

static const struct phy_ops inno_video_phy_ops = {
	.set_mode = inno_video_phy_set_mode,
	.power_on = inno_video_phy_power_on,
	.power_off = inno_video_phy_power_off,
	.owner = THIS_MODULE,
};

static const struct regmap_config inno_video_phy_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = 0x0180,
};

static int inno_video_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct inno_video_phy *inno;
	struct phy *phy;
	struct phy_provider *phy_provider;
	struct resource *res;
	void __iomem *regs;
	int ret;

	inno = devm_kzalloc(dev, sizeof(*inno), GFP_KERNEL);
	if (!inno)
		return -ENOMEM;

	inno->dev = dev;
	platform_set_drvdata(pdev, inno);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	inno->regmap = devm_regmap_init_mmio(dev, regs,
					     &inno_video_phy_regmap_config);
	if (IS_ERR(inno->regmap)) {
		ret = PTR_ERR(inno->regmap);
		dev_err(dev, "failed to init regmap: %d\n", ret);
		return ret;
	}

	inno->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(inno->pclk)) {
		dev_err(dev, "failed to get pclk\n");
		return PTR_ERR(inno->pclk);
	}

	inno->rst = devm_reset_control_get(dev, "rst");
	if (IS_ERR(inno->rst)) {
		dev_err(dev, "failed to get reset control\n");
		return PTR_ERR(inno->rst);
	}

	phy = devm_phy_create(dev, NULL, &inno_video_phy_ops);
	if (IS_ERR(phy)) {
		ret = PTR_ERR(phy);
		dev_err(dev, "failed to create PHY: %d\n", ret);
		return ret;
	}

	phy_set_drvdata(phy, inno);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		dev_err(dev, "failed to register phy provider\n");
		return PTR_ERR(phy_provider);
	}

	pm_runtime_enable(dev);

	return 0;
}

static int inno_video_phy_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct of_device_id inno_video_phy_of_match[] = {
	{ .compatible = "rockchip,rk3288-video-phy", },
	{}
};
MODULE_DEVICE_TABLE(of, inno_video_phy_of_match);

static struct platform_driver inno_video_phy_driver = {
	.driver = {
		.name = "inno-video-phy",
		.of_match_table	= of_match_ptr(inno_video_phy_of_match),
	},
	.probe = inno_video_phy_probe,
	.remove = inno_video_phy_remove,
};
module_platform_driver(inno_video_phy_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Innosilicon LVDS/TTL PHY driver");
MODULE_LICENSE("GPL v2");
