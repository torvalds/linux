// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 MediaTek Inc.
 * Author: Stanley Chu <stanley.chu@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#include "phy-mtk-io.h"

/* mphy register and offsets */
#define MP_GLB_DIG_8C               0x008C
#define FRC_PLL_ISO_EN              BIT(8)
#define PLL_ISO_EN                  BIT(9)
#define FRC_FRC_PWR_ON              BIT(10)
#define PLL_PWR_ON                  BIT(11)

#define MP_LN_DIG_RX_9C             0xA09C
#define FSM_DIFZ_FRC                BIT(18)

#define MP_LN_DIG_RX_AC             0xA0AC
#define FRC_RX_SQ_EN                BIT(0)
#define RX_SQ_EN                    BIT(1)

#define MP_LN_RX_44                 0xB044
#define FRC_CDR_PWR_ON              BIT(17)
#define CDR_PWR_ON                  BIT(18)
#define FRC_CDR_ISO_EN              BIT(19)
#define CDR_ISO_EN                  BIT(20)

#define UFSPHY_CLKS_CNT    2

struct ufs_mtk_phy {
	struct device *dev;
	void __iomem *mmio;
	struct clk_bulk_data clks[UFSPHY_CLKS_CNT];
};

static struct ufs_mtk_phy *get_ufs_mtk_phy(struct phy *generic_phy)
{
	return (struct ufs_mtk_phy *)phy_get_drvdata(generic_phy);
}

static int ufs_mtk_phy_clk_init(struct ufs_mtk_phy *phy)
{
	struct device *dev = phy->dev;
	struct clk_bulk_data *clks = phy->clks;

	clks[0].id = "unipro";
	clks[1].id = "mp";
	return devm_clk_bulk_get(dev, UFSPHY_CLKS_CNT, clks);
}

static void ufs_mtk_phy_set_active(struct ufs_mtk_phy *phy)
{
	void __iomem *mmio = phy->mmio;

	/* release DA_MP_PLL_PWR_ON */
	mtk_phy_set_bits(mmio + MP_GLB_DIG_8C, PLL_PWR_ON);
	mtk_phy_clear_bits(mmio + MP_GLB_DIG_8C, FRC_FRC_PWR_ON);

	/* release DA_MP_PLL_ISO_EN */
	mtk_phy_clear_bits(mmio + MP_GLB_DIG_8C, PLL_ISO_EN);
	mtk_phy_clear_bits(mmio + MP_GLB_DIG_8C, FRC_PLL_ISO_EN);

	/* release DA_MP_CDR_PWR_ON */
	mtk_phy_set_bits(mmio + MP_LN_RX_44, CDR_PWR_ON);
	mtk_phy_clear_bits(mmio + MP_LN_RX_44, FRC_CDR_PWR_ON);

	/* release DA_MP_CDR_ISO_EN */
	mtk_phy_clear_bits(mmio + MP_LN_RX_44, CDR_ISO_EN);
	mtk_phy_clear_bits(mmio + MP_LN_RX_44, FRC_CDR_ISO_EN);

	/* release DA_MP_RX0_SQ_EN */
	mtk_phy_set_bits(mmio + MP_LN_DIG_RX_AC, RX_SQ_EN);
	mtk_phy_clear_bits(mmio + MP_LN_DIG_RX_AC, FRC_RX_SQ_EN);

	/* delay 1us to wait DIFZ stable */
	udelay(1);

	/* release DIFZ */
	mtk_phy_clear_bits(mmio + MP_LN_DIG_RX_9C, FSM_DIFZ_FRC);
}

static void ufs_mtk_phy_set_deep_hibern(struct ufs_mtk_phy *phy)
{
	void __iomem *mmio = phy->mmio;

	/* force DIFZ */
	mtk_phy_set_bits(mmio + MP_LN_DIG_RX_9C, FSM_DIFZ_FRC);

	/* force DA_MP_RX0_SQ_EN */
	mtk_phy_set_bits(mmio + MP_LN_DIG_RX_AC, FRC_RX_SQ_EN);
	mtk_phy_clear_bits(mmio + MP_LN_DIG_RX_AC, RX_SQ_EN);

	/* force DA_MP_CDR_ISO_EN */
	mtk_phy_set_bits(mmio + MP_LN_RX_44, FRC_CDR_ISO_EN);
	mtk_phy_set_bits(mmio + MP_LN_RX_44, CDR_ISO_EN);

	/* force DA_MP_CDR_PWR_ON */
	mtk_phy_set_bits(mmio + MP_LN_RX_44, FRC_CDR_PWR_ON);
	mtk_phy_clear_bits(mmio + MP_LN_RX_44, CDR_PWR_ON);

	/* force DA_MP_PLL_ISO_EN */
	mtk_phy_set_bits(mmio + MP_GLB_DIG_8C, FRC_PLL_ISO_EN);
	mtk_phy_set_bits(mmio + MP_GLB_DIG_8C, PLL_ISO_EN);

	/* force DA_MP_PLL_PWR_ON */
	mtk_phy_set_bits(mmio + MP_GLB_DIG_8C, FRC_FRC_PWR_ON);
	mtk_phy_clear_bits(mmio + MP_GLB_DIG_8C, PLL_PWR_ON);
}

static int ufs_mtk_phy_power_on(struct phy *generic_phy)
{
	struct ufs_mtk_phy *phy = get_ufs_mtk_phy(generic_phy);
	int ret;

	ret = clk_bulk_prepare_enable(UFSPHY_CLKS_CNT, phy->clks);
	if (ret)
		return ret;

	ufs_mtk_phy_set_active(phy);

	return 0;
}

static int ufs_mtk_phy_power_off(struct phy *generic_phy)
{
	struct ufs_mtk_phy *phy = get_ufs_mtk_phy(generic_phy);

	ufs_mtk_phy_set_deep_hibern(phy);

	clk_bulk_disable_unprepare(UFSPHY_CLKS_CNT, phy->clks);

	return 0;
}

static const struct phy_ops ufs_mtk_phy_ops = {
	.power_on       = ufs_mtk_phy_power_on,
	.power_off      = ufs_mtk_phy_power_off,
	.owner          = THIS_MODULE,
};

static int ufs_mtk_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;
	struct ufs_mtk_phy *phy;
	int ret;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(phy->mmio))
		return PTR_ERR(phy->mmio);

	phy->dev = dev;

	ret = ufs_mtk_phy_clk_init(phy);
	if (ret)
		return ret;

	generic_phy = devm_phy_create(dev, NULL, &ufs_mtk_phy_ops);
	if (IS_ERR(generic_phy))
		return PTR_ERR(generic_phy);

	phy_set_drvdata(generic_phy, phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id ufs_mtk_phy_of_match[] = {
	{.compatible = "mediatek,mt8183-ufsphy"},
	{},
};
MODULE_DEVICE_TABLE(of, ufs_mtk_phy_of_match);

static struct platform_driver ufs_mtk_phy_driver = {
	.probe = ufs_mtk_phy_probe,
	.driver = {
		.of_match_table = ufs_mtk_phy_of_match,
		.name = "ufs_mtk_phy",
	},
};
module_platform_driver(ufs_mtk_phy_driver);

MODULE_DESCRIPTION("Universal Flash Storage (UFS) MediaTek MPHY");
MODULE_AUTHOR("Stanley Chu <stanley.chu@mediatek.com>");
MODULE_LICENSE("GPL v2");
