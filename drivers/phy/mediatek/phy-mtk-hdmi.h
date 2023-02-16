/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Chunhui Dai <chunhui.dai@mediatek.com>
 */

#ifndef _MTK_HDMI_PHY_H
#define _MTK_HDMI_PHY_H
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/types.h>

struct mtk_hdmi_phy;

struct mtk_hdmi_phy_conf {
	unsigned long flags;
	bool pll_default_off;
	const struct clk_ops *hdmi_phy_clk_ops;
	void (*hdmi_phy_enable_tmds)(struct mtk_hdmi_phy *hdmi_phy);
	void (*hdmi_phy_disable_tmds)(struct mtk_hdmi_phy *hdmi_phy);
	int (*hdmi_phy_configure)(struct phy *phy, union phy_configure_opts *opts);
};

struct mtk_hdmi_phy {
	void __iomem *regs;
	struct device *dev;
	struct mtk_hdmi_phy_conf *conf;
	struct clk *pll;
	struct clk_hw pll_hw;
	unsigned long pll_rate;
	unsigned char drv_imp_clk;
	unsigned char drv_imp_d2;
	unsigned char drv_imp_d1;
	unsigned char drv_imp_d0;
	unsigned int ibias;
	unsigned int ibias_up;
	bool tmds_over_340M;
};

struct mtk_hdmi_phy *to_mtk_hdmi_phy(struct clk_hw *hw);

extern struct mtk_hdmi_phy_conf mtk_hdmi_phy_8195_conf;
extern struct mtk_hdmi_phy_conf mtk_hdmi_phy_8173_conf;
extern struct mtk_hdmi_phy_conf mtk_hdmi_phy_2701_conf;

#endif /* _MTK_HDMI_PHY_H */
