/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Jie Qiu <jie.qiu@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef _MTK_HDMI_CTRL_H
#define _MTK_HDMI_CTRL_H
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>

struct mtk_hdmi_phy_conf {
	bool tz_enabled;
	const struct clk_ops *hdmi_phy_clk_ops;
	const struct phy_ops *hdmi_phy_dev_ops;
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
};

struct platform_driver;

extern struct platform_driver mtk_cec_driver;
extern struct platform_driver mtk_hdmi_ddc_driver;
extern struct platform_driver mtk_hdmi_phy_driver;
extern struct mtk_hdmi_phy_conf mtk_hdmi_phy_8173_conf;
extern struct mtk_hdmi_phy_conf mtk_hdmi_phy_2701_conf;


#endif /* _MTK_HDMI_CTRL_H */
