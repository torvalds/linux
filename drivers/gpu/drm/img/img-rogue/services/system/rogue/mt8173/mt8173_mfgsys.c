/*
* Copyright (c) 2014 MediaTek Inc.
* Author: Chiawen Lee <chiawen.lee@mediatek.com>
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

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/thermal.h>

#include "mt8173_mfgsys.h"

static const char * const top_mfg_clk_name[] = {
	"mfg_mem_in_sel",
	"mfg_axi_in_sel",
	"top_axi",
	"top_mem",
};

#define MAX_TOP_MFG_CLK ARRAY_SIZE(top_mfg_clk_name)

#define REG_MFG_AXI BIT(0)
#define REG_MFG_MEM BIT(1)
#define REG_MFG_G3D BIT(2)
#define REG_MFG_26M BIT(3)
#define REG_MFG_ALL (REG_MFG_AXI | REG_MFG_MEM | REG_MFG_G3D | REG_MFG_26M)

#define REG_MFG_CG_STA 0x00
#define REG_MFG_CG_SET 0x04
#define REG_MFG_CG_CLR 0x08

static void mtk_mfg_clr_clock_gating(void __iomem *reg)
{
	writel(REG_MFG_ALL, reg + REG_MFG_CG_CLR);
}

static int mtk_mfg_prepare_clock(struct mtk_mfg *mfg)
{
	int i;
	int ret;

	for (i = 0; i < MAX_TOP_MFG_CLK; i++) {
		ret = clk_prepare(mfg->top_clk[i]);
		if (ret)
			goto unwind;
	}
	ret = clk_prepare(mfg->top_mfg);
	if (ret)
		goto unwind;

	return 0;
unwind:
	while (i--)
		clk_unprepare(mfg->top_clk[i]);

	return ret;
}

static void mtk_mfg_unprepare_clock(struct mtk_mfg *mfg)
{
	int i;

	clk_unprepare(mfg->top_mfg);
	for (i = MAX_TOP_MFG_CLK - 1; i >= 0; i--)
		clk_unprepare(mfg->top_clk[i]);
}

static int mtk_mfg_enable_clock(struct mtk_mfg *mfg)
{
	int i;
	int ret;

	for (i = 0; i < MAX_TOP_MFG_CLK; i++) {
		ret = clk_enable(mfg->top_clk[i]);
		if (ret)
			goto unwind;
	}
	ret = clk_enable(mfg->top_mfg);
	if (ret)
		goto unwind;
	mtk_mfg_clr_clock_gating(mfg->reg_base);

	return 0;
unwind:
	while (i--)
		clk_disable(mfg->top_clk[i]);

	return ret;
}

static void mtk_mfg_disable_clock(struct mtk_mfg *mfg)
{
	int i;

	clk_disable(mfg->top_mfg);
	for (i = MAX_TOP_MFG_CLK - 1; i >= 0; i--)
		clk_disable(mfg->top_clk[i]);
}

static void mtk_mfg_enable_hw_apm(struct mtk_mfg *mfg)
{
	writel(0x003c3d4d, mfg->reg_base + 0x24);
	writel(0x4d45440b, mfg->reg_base + 0x28);
	writel(0x7a710184, mfg->reg_base + 0xe0);
	writel(0x835f6856, mfg->reg_base + 0xe4);
	writel(0x002b0234, mfg->reg_base + 0xe8);
	writel(0x80000000, mfg->reg_base + 0xec);
	writel(0x08000000, mfg->reg_base + 0xa0);
}

int mtk_mfg_enable(struct mtk_mfg *mfg)
{
	int ret;

	ret = pm_runtime_get_sync(mfg->dev);
	if (ret)
		return ret;

	ret = mtk_mfg_enable_clock(mfg);
	if (ret)
		goto err_pm_runtime_put;

	mtk_mfg_enable_hw_apm(mfg);

	return 0;

err_pm_runtime_put:
	pm_runtime_put_sync(mfg->dev);
	return ret;
}

static void mtk_mfg_disable_hw_apm(struct mtk_mfg *mfg)
{
	writel(0x00, mfg->reg_base + 0xec);
}

void mtk_mfg_disable(struct mtk_mfg *mfg)
{
	mtk_mfg_disable_hw_apm(mfg);

	mtk_mfg_disable_clock(mfg);
	pm_runtime_put_sync(mfg->dev);
}

int mtk_mfg_freq_set(struct mtk_mfg *mfg, unsigned long freq)
{
	int ret;

	ret = clk_prepare_enable(mfg->top_mfg);
	if (ret) {
		dev_err(mfg->dev, "enable and prepare top_mfg failed, %d\n", ret);
		return ret;
	}

	ret = clk_set_parent(mfg->top_mfg, mfg->clk26m);
	if (ret) {
		dev_err(mfg->dev, "Set clk parent to clk26m failed, %d\n", ret);
		goto unprepare_top_mfg;
	}

	ret = clk_set_rate(mfg->mmpll, freq);
	if (ret)
		dev_err(mfg->dev, "Set freq to %lu Hz failed, %d\n", freq, ret);

	ret = clk_set_parent(mfg->top_mfg, mfg->top_mmpll);
	if (ret)
		dev_err(mfg->dev, "Set clk parent to top_mmpll failed, %d\n", ret);

unprepare_top_mfg:
	clk_disable_unprepare(mfg->top_mfg);

	if (!ret)
		dev_dbg(mfg->dev, "Freq set to %lu Hz\n", freq);

	return ret;
}

int mtk_mfg_volt_set(struct mtk_mfg *mfg, int volt)
{
	int ret;

	ret = regulator_set_voltage(mfg->vgpu, volt, volt);
	if (ret != 0) {
		dev_err(mfg->dev, "Set voltage to %u uV failed, %d\n",
			volt, ret);
		return ret;
	}

	dev_dbg(mfg->dev, "Voltage set to %d uV\n", volt);

	return 0;
}

static int mtk_mfg_bind_device_resource(struct mtk_mfg *mfg)
{
	struct device *dev = mfg->dev;
	struct platform_device *pdev = to_platform_device(dev);
	int i;
	struct resource *res;

	mfg->top_clk = devm_kcalloc(dev, MAX_TOP_MFG_CLK,
				    sizeof(*mfg->top_clk), GFP_KERNEL);
	if (!mfg->top_clk)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	mfg->rgx_start = res->start;
	mfg->rgx_size = resource_size(res);

	mfg->rgx_irq = platform_get_irq_byname(pdev, "RGX");
	if (mfg->rgx_irq < 0)
		return mfg->rgx_irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	mfg->reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(mfg->reg_base))
		return PTR_ERR(mfg->reg_base);

	mfg->mmpll = devm_clk_get(dev, "mmpll_clk");
	if (IS_ERR(mfg->mmpll)) {
		dev_err(dev, "devm_clk_get mmpll_clk failed !!!\n");
		return PTR_ERR(mfg->mmpll);
	}

	for (i = 0; i < MAX_TOP_MFG_CLK; i++) {
		mfg->top_clk[i] = devm_clk_get(dev, top_mfg_clk_name[i]);
		if (IS_ERR(mfg->top_clk[i])) {
			dev_err(dev, "devm_clk_get %s failed !!!\n",
				top_mfg_clk_name[i]);
			return PTR_ERR(mfg->top_clk[i]);
		}
	}

	mfg->top_mfg = devm_clk_get(dev, "top_mfg");
	if (IS_ERR(mfg->top_mfg)) {
		dev_err(dev, "devm_clk_get top_mfg failed !!!\n");
		return PTR_ERR(mfg->top_mfg);
	}

	mfg->top_mmpll = devm_clk_get(dev, "top_mmpll");
	if (IS_ERR(mfg->top_mmpll)) {
		dev_err(dev, "devm_clk_get top_mmpll failed !!!\n");
		return PTR_ERR(mfg->top_mmpll);
	}

	mfg->clk26m = devm_clk_get(dev, "clk26m");
	if (IS_ERR(mfg->clk26m)) {
		dev_err(dev, "devm_clk_get clk26m failed !!!\n");
		return PTR_ERR(mfg->clk26m);
	}

#if defined(CONFIG_DEVFREQ_THERMAL)
	mfg->tz = thermal_zone_get_zone_by_name("cpu_thermal");
	if (IS_ERR(mfg->tz)) {
		dev_warn(dev, "Failed to get cpu_thermal zone\n");
	}
#endif

	mfg->vgpu = devm_regulator_get(dev, "mfgsys-power");
	if (IS_ERR(mfg->vgpu))
		return PTR_ERR(mfg->vgpu);

	pm_runtime_enable(dev);

	return 0;
}

static void mtk_mfg_unbind_device_resource(struct mtk_mfg *mfg)
{
	struct device *dev = mfg->dev;

	pm_runtime_disable(dev);
}

struct mtk_mfg *mtk_mfg_create(struct device *dev)
{
	int err;
	struct mtk_mfg *mfg;

	mtk_mfg_debug("mtk_mfg_create Begin\n");

	mfg = devm_kzalloc(dev, sizeof(*mfg), GFP_KERNEL);
	if (!mfg)
		return ERR_PTR(-ENOMEM);
	mfg->dev = dev;

	err = mtk_mfg_bind_device_resource(mfg);
	if (err != 0)
		return ERR_PTR(err);

	mutex_init(&mfg->set_power_state);

	err = mtk_mfg_prepare_clock(mfg);
	if (err)
		goto err_unbind_resource;

	mtk_mfg_debug("mtk_mfg_create End\n");

	return mfg;
err_unbind_resource:
	mtk_mfg_unbind_device_resource(mfg);

	return ERR_PTR(err);
}

void mtk_mfg_destroy(struct mtk_mfg *mfg)
{
	mtk_mfg_unprepare_clock(mfg);

	mtk_mfg_unbind_device_resource(mfg);
}
