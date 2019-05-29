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
	struct devfreq_event_desc *desc;
	struct dmc_usage ch_usage[RK3399_DMC_NUM_CH];
	struct device *dev;
	void __iomem *regs;
	struct regmap *regmap_pmu;
	struct clk *clk;
};

static void rockchip_dfi_start_hardware_counter(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);
	void __iomem *dfi_regs = info->regs;
	u32 val;
	u32 ddr_type;

	/* get ddr type */
	regmap_read(info->regmap_pmu, RK3399_PMUGRF_OS_REG2, &val);
	ddr_type = (val >> RK3399_PMUGRF_DDRTYPE_SHIFT) &
		    RK3399_PMUGRF_DDRTYPE_MASK;

	/* clear DDRMON_CTRL setting */
	writel_relaxed(CLR_DDRMON_CTRL, dfi_regs + DDRMON_CTRL);

	/* set ddr type to dfi */
	if (ddr_type == RK3399_PMUGRF_DDRTYPE_LPDDR3)
		writel_relaxed(LPDDR3_EN, dfi_regs + DDRMON_CTRL);
	else if (ddr_type == RK3399_PMUGRF_DDRTYPE_LPDDR4)
		writel_relaxed(LPDDR4_EN, dfi_regs + DDRMON_CTRL);

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
	for (i = 0; i < RK3399_DMC_NUM_CH; i++) {
		info->ch_usage[i].access = readl_relaxed(dfi_regs +
				DDRMON_CH0_DFI_ACCESS_NUM + i * 20) * 4;
		info->ch_usage[i].total = readl_relaxed(dfi_regs +
				DDRMON_CH0_COUNT_NUM + i * 20);
		tmp = info->ch_usage[i].access;
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
	clk_disable_unprepare(info->clk);

	return 0;
}

static int rockchip_dfi_enable(struct devfreq_event_dev *edev)
{
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);
	int ret;

	ret = clk_prepare_enable(info->clk);
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
	struct rockchip_dfi *info = devfreq_event_get_drvdata(edev);
	int busier_ch;

	busier_ch = rockchip_dfi_get_busier_ch(edev);

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

static const struct of_device_id rockchip_dfi_id_match[] = {
	{ .compatible = "rockchip,rk3399-dfi" },
	{ },
};
MODULE_DEVICE_TABLE(of, rockchip_dfi_id_match);

static int rockchip_dfi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rockchip_dfi *data;
	struct resource *res;
	struct devfreq_event_desc *desc;
	struct device_node *np = pdev->dev.of_node, *node;

	data = devm_kzalloc(dev, sizeof(struct rockchip_dfi), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

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
	data->dev = dev;

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	desc->ops = &rockchip_dfi_ops;
	desc->driver_data = data;
	desc->name = np->name;
	data->desc = desc;

	data->edev = devm_devfreq_event_add_edev(&pdev->dev, desc);
	if (IS_ERR(data->edev)) {
		dev_err(&pdev->dev,
			"failed to add devfreq-event device\n");
		return PTR_ERR(data->edev);
	}

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
