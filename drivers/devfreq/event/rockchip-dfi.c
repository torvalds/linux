/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Lin Huang <hl@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/clk.h>
#include <linux/devfreq-event.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/of.h>

#define PX30_PMUGRF_OS_REG2		0x208

#define RK3128_GRF_SOC_CON0		0x140
#define RK3128_GRF_OS_REG1		0x1cc
#define RK3128_GRF_DFI_WRNUM		0x220
#define RK3128_GRF_DFI_RDNUM		0x224
#define RK3128_GRF_DFI_TIMERVAL		0x22c
#define RK3128_DDR_MONITOR_EN		((1 << (16 + 6)) + (1 << 6))
#define RK3128_DDR_MONITOR_DISB		((1 << (16 + 6)) + (0 << 6))

#define RK3288_PMU_SYS_REG2		0x9c
#define RK3288_GRF_SOC_CON4		0x254
#define RK3288_GRF_SOC_STATUS(n)	(0x280 + (n) * 4)
#define RK3288_DFI_EN			(0x30003 << 14)
#define RK3288_DFI_DIS			(0x30000 << 14)
#define RK3288_LPDDR_SEL		(0x10001 << 13)
#define RK3288_DDR3_SEL			(0x10000 << 13)

#define RK3328_GRF_OS_REG2		0x5d0

#define RK3368_GRF_DDRC0_CON0		0x600
#define RK3368_GRF_SOC_STATUS5		0x494
#define RK3368_GRF_SOC_STATUS6		0x498
#define RK3368_GRF_SOC_STATUS8		0x4a0
#define RK3368_GRF_SOC_STATUS9		0x4a4
#define RK3368_GRF_SOC_STATUS10		0x4a8
#define RK3368_DFI_EN			(0x30003 << 5)
#define RK3368_DFI_DIS			(0x30000 << 5)

#define MAX_DMC_NUM_CH			2
#define READ_DRAMTYPE_INFO(n)		(((n) >> 13) & 0x7)
#define READ_CH_INFO(n)			(((n) >> 28) & 0x3)
/* DDRMON_CTRL */
#define DDRMON_CTRL			0x04
#define CLR_DDRMON_CTRL			(0x3f0000 << 0)
#define DDR4_EN				(0x10001 << 5)
#define LPDDR4_EN			(0x10001 << 4)
#define HARDWARE_EN			(0x10001 << 3)
#define LPDDR2_3_EN			(0x10001 << 2)
#define SOFTWARE_EN			(0x10001 << 1)
#define SOFTWARE_DIS			(0x10000 << 1)
#define TIME_CNT_EN			(0x10001 << 0)

#define DDRMON_CH0_COUNT_NUM		0x28
#define DDRMON_CH0_DFI_ACCESS_NUM	0x2c
#define DDRMON_CH1_COUNT_NUM		0x3c
#define DDRMON_CH1_DFI_ACCESS_NUM	0x40

/* pmu grf */
#define PMUGRF_OS_REG2			0x308

enum {
	DDR4 = 0,
	DDR3 = 3,
	LPDDR2 = 5,
	LPDDR3 = 6,
	LPDDR4 = 7,
	UNUSED = 0xFF
};

struct dmc_usage {
	u32 access;
	u32 total;
};

/*
 * The dfi controller can monitor DDR load. It has an upper and lower threshold
 * for the operating points. Whenever the usage leaves these bounds an event is
 * generated to indicate the DDR frequency should be changed.
 */
struct rockchip_dfi {
	struct devfreq_event_dev *edev;
	struct devfreq_event_desc *desc;
	struct dmc_usage ch_usage[MAX_DMC_NUM_CH];
	struct device *dev;
	void __iomem *regs;
	struct regmap *regmap_pmu;
	struct regmap *regmap_grf;
	struct regmap *regmap_pmugrf;
	struct clk *clk;
	u32 dram_type;
	/*
	 * available mask, 1: available, 0: not available
	 * each bit represent a channel
	 */
	u32 ch_msk;
};

static void rk3128_dfi_start_hardware_counter(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);

	regmap_write(info->regmap_grf,
		     RK3128_GRF_SOC_CON0,
		     RK3128_DDR_MONITOR_EN);
}

static void rk3128_dfi_stop_hardware_counter(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);

	regmap_write(info->regmap_grf,
		     RK3128_GRF_SOC_CON0,
		     RK3128_DDR_MONITOR_DISB);
}

static int rk3128_dfi_disable(struct devfreq_event_dev *edev)
{
	rk3128_dfi_stop_hardware_counter(edev);

	return 0;
}

static int rk3128_dfi_enable(struct devfreq_event_dev *edev)
{
	rk3128_dfi_start_hardware_counter(edev);

	return 0;
}

static int rk3128_dfi_set_event(struct devfreq_event_dev *edev)
{
	return 0;
}

static int rk3128_dfi_get_event(struct devfreq_event_dev *edev,
				struct devfreq_event_data *edata)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);
	unsigned long flags;
	u32 dfi_wr, dfi_rd, dfi_timer;

	local_irq_save(flags);

	rk3128_dfi_stop_hardware_counter(edev);

	regmap_read(info->regmap_grf, RK3128_GRF_DFI_WRNUM, &dfi_wr);
	regmap_read(info->regmap_grf, RK3128_GRF_DFI_RDNUM, &dfi_rd);
	regmap_read(info->regmap_grf, RK3128_GRF_DFI_TIMERVAL, &dfi_timer);

	edata->load_count = (dfi_wr + dfi_rd) * 4;
	edata->total_count = dfi_timer;

	rk3128_dfi_start_hardware_counter(edev);

	local_irq_restore(flags);

	return 0;
}

static const struct devfreq_event_ops rk3128_dfi_ops = {
	.disable = rk3128_dfi_disable,
	.enable = rk3128_dfi_enable,
	.get_event = rk3128_dfi_get_event,
	.set_event = rk3128_dfi_set_event,
};

static void rk3288_dfi_start_hardware_counter(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);

	regmap_write(info->regmap_grf, RK3288_GRF_SOC_CON4, RK3288_DFI_EN);
}

static void rk3288_dfi_stop_hardware_counter(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);

	regmap_write(info->regmap_grf, RK3288_GRF_SOC_CON4, RK3288_DFI_DIS);
}

static int rk3288_dfi_disable(struct devfreq_event_dev *edev)
{
	rk3288_dfi_stop_hardware_counter(edev);

	return 0;
}

static int rk3288_dfi_enable(struct devfreq_event_dev *edev)
{
	rk3288_dfi_start_hardware_counter(edev);

	return 0;
}

static int rk3288_dfi_set_event(struct devfreq_event_dev *edev)
{
	return 0;
}

static int rk3288_dfi_get_busier_ch(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);
	u32 tmp, max = 0;
	u32 i, busier_ch = 0;
	u32 rd_count, wr_count, total_count;

	rk3288_dfi_stop_hardware_counter(edev);

	/* Find out which channel is busier */
	for (i = 0; i < MAX_DMC_NUM_CH; i++) {
		if (!(info->ch_msk & BIT(i)))
			continue;
		regmap_read(info->regmap_grf,
			    RK3288_GRF_SOC_STATUS(11 + i * 4), &wr_count);
		regmap_read(info->regmap_grf,
			    RK3288_GRF_SOC_STATUS(12 + i * 4), &rd_count);
		regmap_read(info->regmap_grf,
			    RK3288_GRF_SOC_STATUS(14 + i * 4), &total_count);
		info->ch_usage[i].access = (wr_count + rd_count) * 4;
		info->ch_usage[i].total = total_count;
		tmp = info->ch_usage[i].access;
		if (tmp > max) {
			busier_ch = i;
			max = tmp;
		}
	}
	rk3288_dfi_start_hardware_counter(edev);

	return busier_ch;
}

static int rk3288_dfi_get_event(struct devfreq_event_dev *edev,
				struct devfreq_event_data *edata)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);
	int busier_ch;
	unsigned long flags;

	local_irq_save(flags);
	busier_ch = rk3288_dfi_get_busier_ch(edev);
	local_irq_restore(flags);

	edata->load_count = info->ch_usage[busier_ch].access;
	edata->total_count = info->ch_usage[busier_ch].total;

	return 0;
}

static const struct devfreq_event_ops rk3288_dfi_ops = {
	.disable = rk3288_dfi_disable,
	.enable = rk3288_dfi_enable,
	.get_event = rk3288_dfi_get_event,
	.set_event = rk3288_dfi_set_event,
};

static void rk3368_dfi_start_hardware_counter(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);

	regmap_write(info->regmap_grf, RK3368_GRF_DDRC0_CON0, RK3368_DFI_EN);
}

static void rk3368_dfi_stop_hardware_counter(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);

	regmap_write(info->regmap_grf, RK3368_GRF_DDRC0_CON0, RK3368_DFI_DIS);
}

static int rk3368_dfi_disable(struct devfreq_event_dev *edev)
{
	rk3368_dfi_stop_hardware_counter(edev);

	return 0;
}

static int rk3368_dfi_enable(struct devfreq_event_dev *edev)
{
	rk3368_dfi_start_hardware_counter(edev);

	return 0;
}

static int rk3368_dfi_set_event(struct devfreq_event_dev *edev)
{
	return 0;
}

static int rk3368_dfi_get_event(struct devfreq_event_dev *edev,
				struct devfreq_event_data *edata)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);
	unsigned long flags;
	u32 dfi0_wr, dfi0_rd, dfi1_wr, dfi1_rd, dfi_timer;

	local_irq_save(flags);

	rk3368_dfi_stop_hardware_counter(edev);

	regmap_read(info->regmap_grf, RK3368_GRF_SOC_STATUS5, &dfi0_wr);
	regmap_read(info->regmap_grf, RK3368_GRF_SOC_STATUS6, &dfi0_rd);
	regmap_read(info->regmap_grf, RK3368_GRF_SOC_STATUS9, &dfi1_wr);
	regmap_read(info->regmap_grf, RK3368_GRF_SOC_STATUS10, &dfi1_rd);
	regmap_read(info->regmap_grf, RK3368_GRF_SOC_STATUS8, &dfi_timer);

	edata->load_count = (dfi0_wr + dfi0_rd + dfi1_wr + dfi1_rd) * 2;
	edata->total_count = dfi_timer;

	rk3368_dfi_start_hardware_counter(edev);

	local_irq_restore(flags);

	return 0;
}

static const struct devfreq_event_ops rk3368_dfi_ops = {
	.disable = rk3368_dfi_disable,
	.enable = rk3368_dfi_enable,
	.get_event = rk3368_dfi_get_event,
	.set_event = rk3368_dfi_set_event,
};

static void rockchip_dfi_start_hardware_counter(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);
	void __iomem *dfi_regs = info->regs;

	/* clear DDRMON_CTRL setting */
	writel_relaxed(CLR_DDRMON_CTRL, dfi_regs + DDRMON_CTRL);

	/* set ddr type to dfi */
	if (info->dram_type == LPDDR3 || info->dram_type == LPDDR2)
		writel_relaxed(LPDDR2_3_EN, dfi_regs + DDRMON_CTRL);
	else if (info->dram_type == LPDDR4)
		writel_relaxed(LPDDR4_EN, dfi_regs + DDRMON_CTRL);
	else if (info->dram_type == DDR4)
		writel_relaxed(DDR4_EN, dfi_regs + DDRMON_CTRL);

	/* enable count, use software mode */
	writel_relaxed(SOFTWARE_EN, dfi_regs + DDRMON_CTRL);
}

static void rockchip_dfi_stop_hardware_counter(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);
	void __iomem *dfi_regs = info->regs;

	writel_relaxed(SOFTWARE_DIS, dfi_regs + DDRMON_CTRL);
}

static int rockchip_dfi_get_busier_ch(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);
	u32 tmp, max = 0;
	u32 i, busier_ch = 0;
	void __iomem *dfi_regs = info->regs;

	rockchip_dfi_stop_hardware_counter(edev);

	/* Find out which channel is busier */
	for (i = 0; i < MAX_DMC_NUM_CH; i++) {
		if (!(info->ch_msk & BIT(i)))
			continue;

		info->ch_usage[i].total = readl_relaxed(dfi_regs +
				DDRMON_CH0_COUNT_NUM + i * 20);

		/* LPDDR4 BL = 16,other DDR type BL = 8 */
		tmp = readl_relaxed(dfi_regs +
				DDRMON_CH0_DFI_ACCESS_NUM + i * 20);
		if (info->dram_type == LPDDR4)
			tmp *= 8;
		else
			tmp *= 4;
		info->ch_usage[i].access = tmp;

		if (tmp > max) {
			busier_ch = i;
			max = tmp;
		}
	}
	rockchip_dfi_start_hardware_counter(edev);

	return busier_ch;
}

static int rockchip_dfi_disable(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);

	rockchip_dfi_stop_hardware_counter(edev);
	if (info->clk)
		clk_disable_unprepare(info->clk);

	return 0;
}

static int rockchip_dfi_enable(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);
	int ret;

	if (info->clk) {
		ret = clk_prepare_enable(info->clk);
		if (ret) {
			dev_err(&edev->dev, "failed to enable dfi clk: %d\n",
				ret);
			return ret;
		}
	}

	rockchip_dfi_start_hardware_counter(edev);
	return 0;
}

static int rockchip_dfi_set_event(struct devfreq_event_dev *edev)
{
	return 0;
}

static int rockchip_dfi_get_event(struct devfreq_event_dev *edev,
				  struct devfreq_event_data *edata)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);
	int busier_ch;
	unsigned long flags;

	local_irq_save(flags);
	busier_ch = rockchip_dfi_get_busier_ch(edev);
	local_irq_restore(flags);

	edata->load_count = info->ch_usage[busier_ch].access;
	edata->total_count = info->ch_usage[busier_ch].total;

	return 0;
}

static const struct devfreq_event_ops rockchip_dfi_ops = {
	.disable = rockchip_dfi_disable,
	.enable = rockchip_dfi_enable,
	.get_event = rockchip_dfi_get_event,
	.set_event = rockchip_dfi_set_event,
};

static __init int px30_dfi_init(struct platform_device *pdev,
				  struct rockchip_dfi *data,
				  struct devfreq_event_desc *desc)
{
	struct device_node *np = pdev->dev.of_node, *node;
	struct resource *res;
	u32 val;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->regs))
		return PTR_ERR(data->regs);

	node = of_parse_phandle(np, "rockchip,pmugrf", 0);
	if (node) {
		data->regmap_pmugrf = syscon_node_to_regmap(node);
		if (IS_ERR(data->regmap_pmugrf))
			return PTR_ERR(data->regmap_pmugrf);
	}

	regmap_read(data->regmap_pmugrf, PX30_PMUGRF_OS_REG2, &val);
	data->dram_type = READ_DRAMTYPE_INFO(val);
	data->ch_msk = 1;
	data->clk = NULL;

	desc->ops = &rockchip_dfi_ops;

	return 0;
}

static __init int rk3128_dfi_init(struct platform_device *pdev,
				  struct rockchip_dfi *data,
				  struct devfreq_event_desc *desc)
{
	struct device_node *np = pdev->dev.of_node, *node;

	node = of_parse_phandle(np, "rockchip,grf", 0);
	if (node) {
		data->regmap_grf = syscon_node_to_regmap(node);
		if (IS_ERR(data->regmap_grf))
			return PTR_ERR(data->regmap_grf);
	}

	desc->ops = &rk3128_dfi_ops;

	return 0;
}

static __init int rk3288_dfi_init(struct platform_device *pdev,
				  struct rockchip_dfi *data,
				  struct devfreq_event_desc *desc)
{
	struct device_node *np = pdev->dev.of_node, *node;
	u32 val;

	node = of_parse_phandle(np, "rockchip,pmu", 0);
	if (node) {
		data->regmap_pmu = syscon_node_to_regmap(node);
		if (IS_ERR(data->regmap_pmu))
			return PTR_ERR(data->regmap_pmu);
	}

	node = of_parse_phandle(np, "rockchip,grf", 0);
	if (node) {
		data->regmap_grf = syscon_node_to_regmap(node);
		if (IS_ERR(data->regmap_grf))
			return PTR_ERR(data->regmap_grf);
	}

	regmap_read(data->regmap_pmu, RK3288_PMU_SYS_REG2, &val);
	data->dram_type = READ_DRAMTYPE_INFO(val);
	data->ch_msk = READ_CH_INFO(val);

	if (data->dram_type == DDR3)
		regmap_write(data->regmap_grf, RK3288_GRF_SOC_CON4,
			     RK3288_DDR3_SEL);
	else
		regmap_write(data->regmap_grf, RK3288_GRF_SOC_CON4,
			     RK3288_LPDDR_SEL);

	desc->ops = &rk3288_dfi_ops;

	return 0;
}

static __init int rk3368_dfi_init(struct platform_device *pdev,
				  struct rockchip_dfi *data,
				  struct devfreq_event_desc *desc)
{
	struct device *dev = &pdev->dev;

	if (!dev->parent || !dev->parent->of_node)
		return -EINVAL;

	data->regmap_grf = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(data->regmap_grf))
		return PTR_ERR(data->regmap_grf);

	desc->ops = &rk3368_dfi_ops;

	return 0;
}

static __init int rockchip_dfi_init(struct platform_device *pdev,
				    struct rockchip_dfi *data,
				    struct devfreq_event_desc *desc)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct device_node *np = pdev->dev.of_node, *node;
	u32 val;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->regs))
		return PTR_ERR(data->regs);

	data->clk = devm_clk_get(dev, "pclk_ddr_mon");
	if (IS_ERR(data->clk)) {
		dev_err(dev, "Cannot get the clk dmc_clk\n");
		return PTR_ERR(data->clk);
	}

	/* try to find the optional reference to the pmu syscon */
	node = of_parse_phandle(np, "rockchip,pmu", 0);
	if (node) {
		data->regmap_pmu = syscon_node_to_regmap(node);
		if (IS_ERR(data->regmap_pmu))
			return PTR_ERR(data->regmap_pmu);
	}

	regmap_read(data->regmap_pmu, PMUGRF_OS_REG2, &val);
	data->dram_type = READ_DRAMTYPE_INFO(val);
	data->ch_msk = READ_CH_INFO(val);

	desc->ops = &rockchip_dfi_ops;

	return 0;
}

static __init int rk3328_dfi_init(struct platform_device *pdev,
				  struct rockchip_dfi *data,
				  struct devfreq_event_desc *desc)
{
	struct device_node *np = pdev->dev.of_node, *node;
	struct resource *res;
	u32 val;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->regs))
		return PTR_ERR(data->regs);

	node = of_parse_phandle(np, "rockchip,grf", 0);
	if (node) {
		data->regmap_grf = syscon_node_to_regmap(node);
		if (IS_ERR(data->regmap_grf))
			return PTR_ERR(data->regmap_grf);
	}

	regmap_read(data->regmap_grf, RK3328_GRF_OS_REG2, &val);
	data->dram_type = READ_DRAMTYPE_INFO(val);
	data->ch_msk = 1;
	data->clk = NULL;

	desc->ops = &rockchip_dfi_ops;

	return 0;
}

static const struct of_device_id rockchip_dfi_id_match[] = {
	{ .compatible = "rockchip,px30-dfi", .data = px30_dfi_init },
	{ .compatible = "rockchip,rk1808-dfi", .data = px30_dfi_init },
	{ .compatible = "rockchip,rk3128-dfi", .data = rk3128_dfi_init },
	{ .compatible = "rockchip,rk3288-dfi", .data = rk3288_dfi_init },
	{ .compatible = "rockchip,rk3328-dfi", .data = rk3328_dfi_init },
	{ .compatible = "rockchip,rk3368-dfi", .data = rk3368_dfi_init },
	{ .compatible = "rockchip,rk3399-dfi", .data = rockchip_dfi_init },
	{ },
};

static int rockchip_dfi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_dfi *data;
	struct devfreq_event_desc *desc;
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	int (*init)(struct platform_device *pdev, struct rockchip_dfi *data,
		    struct devfreq_event_desc *desc);

	data = devm_kzalloc(dev, sizeof(struct rockchip_dfi), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	match = of_match_node(rockchip_dfi_id_match, pdev->dev.of_node);
	if (match) {
		init = match->data;
		if (init) {
			if (init(pdev, data, desc))
				return -EINVAL;
		} else {
			return 0;
		}
	} else {
		return 0;
	}

	desc->driver_data = data;
	desc->name = np->name;

	data->edev = devm_devfreq_event_add_edev(dev, desc);
	if (IS_ERR(data->edev)) {
		dev_err(dev, "failed to add devfreq-event device\n");
		return PTR_ERR(data->edev);
	}
	data->desc = desc;
	data->dev = &pdev->dev;

	platform_set_drvdata(pdev, data);

	return 0;
}

static struct platform_driver rockchip_dfi_driver = {
	.probe	= rockchip_dfi_probe,
	.driver = {
		.name	= "rockchip-dfi",
		.of_match_table = rockchip_dfi_id_match,
	},
};
module_platform_driver(rockchip_dfi_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Lin Huang <hl@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip DFI driver");
