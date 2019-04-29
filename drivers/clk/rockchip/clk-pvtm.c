// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/clk-provider.h>

#define CLK_SEL_EXTERNAL_32K		0
#define CLK_SEL_INTERNAL_PVTM		1

#define wr_msk_bit(v, off, msk)  ((v) << (off) | (msk << (16 + (off))))

struct rockchip_clock_pvtm;

struct rockchip_clock_pvtm_info {
	u32 con;
	u32 sta;
	u32 sel_con;
	u32 sel_shift;
	u32 sel_value;
	u32 sel_mask;
	u32 div_shift;
	u32 div_mask;

	u32 (*get_value)(struct rockchip_clock_pvtm *pvtm,
			 unsigned int time_us);
	int (*init_freq)(struct rockchip_clock_pvtm *pvtm);
	int (*sel_enable)(struct rockchip_clock_pvtm *pvtm);
};

struct rockchip_clock_pvtm {
	const struct rockchip_clock_pvtm_info *info;
	struct regmap *grf;
	struct clk *pvtm_clk;
	struct clk *clk;
	unsigned long rate;
};

static unsigned long xin32k_pvtm_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	return 32768;
}

static const struct clk_ops xin32k_pvtm = {
	.recalc_rate = xin32k_pvtm_recalc_rate,
};

static void rockchip_clock_pvtm_delay(unsigned int delay)
{
	unsigned int ms = delay / 1000;
	unsigned int us = delay % 1000;

	if (ms > 0) {
		if (ms < 20)
			us += ms * 1000;
		else
			msleep(ms);
	}

	if (us >= 10)
		usleep_range(us, us + 100);
	else
		udelay(us);
}

static int rockchip_clock_sel_internal_pvtm(struct rockchip_clock_pvtm *pvtm)
{
	int ret = 0;

	ret = regmap_write(pvtm->grf, pvtm->info->sel_con,
			   wr_msk_bit(pvtm->info->sel_value,
				      pvtm->info->sel_shift,
				      pvtm->info->sel_mask));
	if (ret != 0)
		pr_err("%s: fail to write register\n", __func__);

	return ret;
}

/* get pmu pvtm value */
static u32 rockchip_clock_pvtm_get_value(struct rockchip_clock_pvtm *pvtm,
					 u32 time_us)
{
	const struct rockchip_clock_pvtm_info *info = pvtm->info;
	u32 val = 0, sta = 0;
	u32 clk_cnt, check_cnt;

	/* 24m clk ,24cnt=1us */
	clk_cnt = time_us * 24;

	regmap_write(pvtm->grf, info->con + 0x4, clk_cnt);
	regmap_write(pvtm->grf, info->con, wr_msk_bit(3, 0, 0x3));

	rockchip_clock_pvtm_delay(time_us);

	check_cnt = 100;
	while (check_cnt--) {
		regmap_read(pvtm->grf, info->sta, &sta);
		if (sta & 0x1)
			break;
		udelay(4);
	}

	if (check_cnt) {
		regmap_read(pvtm->grf, info->sta + 0x4, &val);
	} else {
		pr_err("%s: wait pvtm_done timeout!\n", __func__);
		val = 0;
	}

	regmap_write(pvtm->grf, info->con, wr_msk_bit(0, 0, 0x3));

	return val;
}

static int rockchip_clock_pvtm_init_freq(struct rockchip_clock_pvtm *pvtm)
{
	u32 pvtm_cnt = 0;
	u32 div, time_us;
	int ret = 0;

	time_us = 1000;
	pvtm_cnt = pvtm->info->get_value(pvtm, time_us);
	pr_debug("get pvtm_cnt = %d\n", pvtm_cnt);

	/* set pvtm_div to get rate */
	div = DIV_ROUND_UP(1000 * pvtm_cnt,  pvtm->rate);
	if (div > pvtm->info->div_mask) {
		pr_err("pvtm_div out of bounary! set max instead\n");
		div = pvtm->info->div_mask;
	}

	pr_debug("set div %d, rate %luKHZ\n", div, pvtm->rate);
	ret = regmap_write(pvtm->grf, pvtm->info->con,
			   wr_msk_bit(div, pvtm->info->div_shift,
				      pvtm->info->div_mask));
	if (ret != 0)
		goto out;

	/* pmu pvtm oscilator enable */
	ret = regmap_write(pvtm->grf, pvtm->info->con,
			   wr_msk_bit(1, 1, 0x1));
	if (ret != 0)
		goto out;

	ret = pvtm->info->sel_enable(pvtm);
out:
	if (ret != 0)
		pr_err("%s: fail to write register\n", __func__);

	return ret;
}

static int clock_pvtm_regitstor(struct device *dev,
				struct rockchip_clock_pvtm *pvtm)
{
	struct clk_init_data init = {};
	struct clk_hw *clk_hw;

	/* Init the xin32k_pvtm */
	pvtm->info->init_freq(pvtm);

	init.parent_names = NULL;
	init.num_parents = 0;
	init.name = "xin32k_pvtm";
	init.ops = &xin32k_pvtm;

	clk_hw = devm_kzalloc(dev, sizeof(*clk_hw), GFP_KERNEL);
	if (!clk_hw)
		return -ENOMEM;
	clk_hw->init = &init;

	/* optional override of the clockname */
	of_property_read_string_index(dev->of_node, "clock-output-names",
				      0, &init.name);
	pvtm->clk = devm_clk_register(dev, clk_hw);
	if (IS_ERR(pvtm->clk))
		return PTR_ERR(pvtm->clk);

	return of_clk_add_provider(dev->of_node, of_clk_src_simple_get,
				   pvtm->clk);
}

static const struct rockchip_clock_pvtm_info rk3368_pvtm_data = {
	.con = 0x180,
	.sta = 0x190,
	.sel_con = 0x100,
	.sel_shift = 6,
	.sel_value = CLK_SEL_INTERNAL_PVTM,
	.sel_mask = 0x1,
	.div_shift = 2,
	.div_mask = 0x3f,

	.sel_enable = rockchip_clock_sel_internal_pvtm,
	.get_value = rockchip_clock_pvtm_get_value,
	.init_freq = rockchip_clock_pvtm_init_freq,
};
MODULE_DEVICE_TABLE(of, rockchip_clock_pvtm_match);

static const struct of_device_id rockchip_clock_pvtm_match[] = {
	{
		.compatible = "rockchip,rk3368-pvtm-clock",
		.data = (void *)&rk3368_pvtm_data,
	},
	{}
};

static int rockchip_clock_pvtm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct rockchip_clock_pvtm *pvtm;
	int error;
	u32 rate;

	pvtm = devm_kzalloc(dev, sizeof(*pvtm), GFP_KERNEL);
	if (!pvtm)
		return -ENOMEM;

	match = of_match_node(rockchip_clock_pvtm_match, np);
	if (!match)
		return -ENXIO;

	pvtm->info = (const struct rockchip_clock_pvtm_info *)match->data;
	if (!pvtm->info)
		return -EINVAL;

	if (!dev->parent || !dev->parent->of_node)
		return -EINVAL;

	pvtm->grf = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(pvtm->grf))
		return PTR_ERR(pvtm->grf);

	if (!of_property_read_u32(np, "pvtm-rate", &rate))
		pvtm->rate  = rate;
	else
		pvtm->rate  = 32768;

	pvtm->pvtm_clk = devm_clk_get(&pdev->dev, "pvtm_pmu_clk");
	if (IS_ERR(pvtm->pvtm_clk)) {
		error = PTR_ERR(pvtm->pvtm_clk);
		if (error != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"failed to get pvtm core clock: %d\n",
				error);
		goto out_probe;
	}

	error = clk_prepare_enable(pvtm->pvtm_clk);
	if (error) {
		dev_err(&pdev->dev, "failed to enable the clock: %d\n",
			error);
		goto out_probe;
	}

	platform_set_drvdata(pdev, pvtm);

	error = clock_pvtm_regitstor(&pdev->dev, pvtm);
	if (error) {
		dev_err(&pdev->dev, "failed to registor clock: %d\n",
			error);
		goto out_clk_put;
	}

	return error;

out_clk_put:
	clk_disable_unprepare(pvtm->pvtm_clk);
out_probe:
	return error;
}

static int rockchip_clock_pvtm_remove(struct platform_device *pdev)
{
	struct rockchip_clock_pvtm *pvtm = platform_get_drvdata(pdev);
	struct device_node *np = pdev->dev.of_node;

	of_clk_del_provider(np);
	clk_disable_unprepare(pvtm->pvtm_clk);

	return 0;
}

static struct platform_driver rockchip_clock_pvtm_driver = {
	.driver = {
		.name = "rockchip-clcok-pvtm",
		.of_match_table = rockchip_clock_pvtm_match,
	},
	.probe = rockchip_clock_pvtm_probe,
	.remove = rockchip_clock_pvtm_remove,
};

module_platform_driver(rockchip_clock_pvtm_driver);

MODULE_DESCRIPTION("Rockchip Clock Pvtm Driver");
MODULE_LICENSE("GPL v2");
