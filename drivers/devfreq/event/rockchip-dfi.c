// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd
 * Author: Lin Huang <hl@rock-chips.com>
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
#include <linux/of_device.h>

#include <soc/rockchip/rk3399_grf.h>

#define RK3399_DMC_NUM_CH	2

/* DDRMON_CTRL */
#define DDRMON_CTRL	0x04
#define CLR_DDRMON_CTRL	(0x1f0000 << 0)
#define LPDDR4_EN	(0x10001 << 4)
#define HARDWARE_EN	(0x10001 << 3)
#define LPDDR3_EN	(0x10001 << 2)
#define SOFTWARE_EN	(0x10001 << 1)
#define SOFTWARE_DIS	(0x10000 << 1)
#define TIME_CNT_EN	(0x10001 << 0)

#define DDRMON_CH0_COUNT_NUM		0x28
#define DDRMON_CH0_DFI_ACCESS_NUM	0x2c
#define DDRMON_CH1_COUNT_NUM		0x3c
#define DDRMON_CH1_DFI_ACCESS_NUM	0x40

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
	struct devfreq_event_desc desc;
	struct dmc_usage ch_usage[RK3399_DMC_NUM_CH];
	struct device *dev;
	void __iomem *regs;
	struct regmap *regmap_pmu;
	struct clk *clk;
	u32 ddr_type;
};

static void rockchip_dfi_start_hardware_counter(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *dfi = devfreq_event_get_drvdata(edev);
	void __iomem *dfi_regs = dfi->regs;

	/* clear DDRMON_CTRL setting */
	writel_relaxed(CLR_DDRMON_CTRL, dfi_regs + DDRMON_CTRL);

	/* set ddr type to dfi */
	if (dfi->ddr_type == RK3399_PMUGRF_DDRTYPE_LPDDR3)
		writel_relaxed(LPDDR3_EN, dfi_regs + DDRMON_CTRL);
	else if (dfi->ddr_type == RK3399_PMUGRF_DDRTYPE_LPDDR4)
		writel_relaxed(LPDDR4_EN, dfi_regs + DDRMON_CTRL);

	/* enable count, use software mode */
	writel_relaxed(SOFTWARE_EN, dfi_regs + DDRMON_CTRL);
}

static void rockchip_dfi_stop_hardware_counter(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *dfi = devfreq_event_get_drvdata(edev);
	void __iomem *dfi_regs = dfi->regs;

	writel_relaxed(SOFTWARE_DIS, dfi_regs + DDRMON_CTRL);
}

static int rockchip_dfi_get_busier_ch(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *dfi = devfreq_event_get_drvdata(edev);
	u32 tmp, max = 0;
	u32 i, busier_ch = 0;
	void __iomem *dfi_regs = dfi->regs;

	rockchip_dfi_stop_hardware_counter(edev);

	/* Find out which channel is busier */
	for (i = 0; i < RK3399_DMC_NUM_CH; i++) {
		dfi->ch_usage[i].access = readl_relaxed(dfi_regs +
				DDRMON_CH0_DFI_ACCESS_NUM + i * 20) * 4;
		dfi->ch_usage[i].total = readl_relaxed(dfi_regs +
				DDRMON_CH0_COUNT_NUM + i * 20);
		tmp = dfi->ch_usage[i].access;
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
	struct rockchip_dfi *dfi = devfreq_event_get_drvdata(edev);

	rockchip_dfi_stop_hardware_counter(edev);
	clk_disable_unprepare(dfi->clk);

	return 0;
}

static int rockchip_dfi_enable(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *dfi = devfreq_event_get_drvdata(edev);
	int ret;

	ret = clk_prepare_enable(dfi->clk);
	if (ret) {
		dev_err(&edev->dev, "failed to enable dfi clk: %d\n", ret);
		return ret;
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
	struct rockchip_dfi *dfi = devfreq_event_get_drvdata(edev);
	int busier_ch;

	busier_ch = rockchip_dfi_get_busier_ch(edev);

	edata->load_count = dfi->ch_usage[busier_ch].access;
	edata->total_count = dfi->ch_usage[busier_ch].total;

	return 0;
}

static const struct devfreq_event_ops rockchip_dfi_ops = {
	.disable = rockchip_dfi_disable,
	.enable = rockchip_dfi_enable,
	.get_event = rockchip_dfi_get_event,
	.set_event = rockchip_dfi_set_event,
};

static int rk3399_dfi_init(struct rockchip_dfi *dfi)
{
	struct regmap *regmap_pmu = dfi->regmap_pmu;
	u32 val;

	dfi->clk = devm_clk_get(dfi->dev, "pclk_ddr_mon");
	if (IS_ERR(dfi->clk))
		return dev_err_probe(dfi->dev, PTR_ERR(dfi->clk),
				     "Cannot get the clk pclk_ddr_mon\n");

	/* get ddr type */
	regmap_read(regmap_pmu, RK3399_PMUGRF_OS_REG2, &val);
	dfi->ddr_type = (val >> RK3399_PMUGRF_DDRTYPE_SHIFT) &
			RK3399_PMUGRF_DDRTYPE_MASK;

	return 0;
};

static const struct of_device_id rockchip_dfi_id_match[] = {
	{ .compatible = "rockchip,rk3399-dfi", .data = rk3399_dfi_init },
	{ },
};
MODULE_DEVICE_TABLE(of, rockchip_dfi_id_match);

static int rockchip_dfi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_dfi *dfi;
	struct devfreq_event_desc *desc;
	struct device_node *np = pdev->dev.of_node, *node;
	int (*soc_init)(struct rockchip_dfi *dfi);
	int ret;

	soc_init = of_device_get_match_data(&pdev->dev);
	if (!soc_init)
		return -EINVAL;

	dfi = devm_kzalloc(dev, sizeof(*dfi), GFP_KERNEL);
	if (!dfi)
		return -ENOMEM;

	dfi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dfi->regs))
		return PTR_ERR(dfi->regs);

	node = of_parse_phandle(np, "rockchip,pmu", 0);
	if (!node)
		return dev_err_probe(&pdev->dev, -ENODEV, "Can't find pmu_grf registers\n");

	dfi->regmap_pmu = syscon_node_to_regmap(node);
	of_node_put(node);
	if (IS_ERR(dfi->regmap_pmu))
		return PTR_ERR(dfi->regmap_pmu);

	dfi->dev = dev;

	desc = &dfi->desc;
	desc->ops = &rockchip_dfi_ops;
	desc->driver_data = dfi;
	desc->name = np->name;

	ret = soc_init(dfi);
	if (ret)
		return ret;

	dfi->edev = devm_devfreq_event_add_edev(&pdev->dev, desc);
	if (IS_ERR(dfi->edev)) {
		dev_err(&pdev->dev,
			"failed to add devfreq-event device\n");
		return PTR_ERR(dfi->edev);
	}

	platform_set_drvdata(pdev, dfi);

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
