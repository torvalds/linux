// SPDX-License-Identifier: GPL-2.0-only
/*
 * exynos-nocp.c - Exynos NoC (Network On Chip) Probe support
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 * Author : Chanwoo Choi <cw00.choi@samsung.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/devfreq-event.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "exynos-nocp.h"

struct exynos_nocp {
	struct devfreq_event_dev *edev;
	struct devfreq_event_desc desc;

	struct device *dev;

	struct regmap *regmap;
	struct clk *clk;
};

/*
 * The devfreq-event ops structure for nocp probe.
 */
static int exynos_nocp_set_event(struct devfreq_event_dev *edev)
{
	struct exynos_nocp *nocp = devfreq_event_get_drvdata(edev);
	int ret;

	/* Disable NoC probe */
	ret = regmap_update_bits(nocp->regmap, NOCP_MAIN_CTL,
				NOCP_MAIN_CTL_STATEN_MASK, 0);
	if (ret < 0) {
		dev_err(nocp->dev, "failed to disable the NoC probe device\n");
		return ret;
	}

	/* Set a statistics dump period to 0 */
	ret = regmap_write(nocp->regmap, NOCP_STAT_PERIOD, 0x0);
	if (ret < 0)
		goto out;

	/* Set the IntEvent fields of *_SRC */
	ret = regmap_update_bits(nocp->regmap, NOCP_COUNTERS_0_SRC,
				NOCP_CNT_SRC_INTEVENT_MASK,
				NOCP_CNT_SRC_INTEVENT_BYTE_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(nocp->regmap, NOCP_COUNTERS_1_SRC,
				NOCP_CNT_SRC_INTEVENT_MASK,
				NOCP_CNT_SRC_INTEVENT_CHAIN_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(nocp->regmap, NOCP_COUNTERS_2_SRC,
				NOCP_CNT_SRC_INTEVENT_MASK,
				NOCP_CNT_SRC_INTEVENT_CYCLE_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(nocp->regmap, NOCP_COUNTERS_3_SRC,
				NOCP_CNT_SRC_INTEVENT_MASK,
				NOCP_CNT_SRC_INTEVENT_CHAIN_MASK);
	if (ret < 0)
		goto out;


	/* Set an alarm with a max/min value of 0 to generate StatALARM */
	ret = regmap_write(nocp->regmap, NOCP_STAT_ALARM_MIN, 0x0);
	if (ret < 0)
		goto out;

	ret = regmap_write(nocp->regmap, NOCP_STAT_ALARM_MAX, 0x0);
	if (ret < 0)
		goto out;

	/* Set AlarmMode */
	ret = regmap_update_bits(nocp->regmap, NOCP_COUNTERS_0_ALARM_MODE,
				NOCP_CNT_ALARM_MODE_MASK,
				NOCP_CNT_ALARM_MODE_MIN_MAX_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(nocp->regmap, NOCP_COUNTERS_1_ALARM_MODE,
				NOCP_CNT_ALARM_MODE_MASK,
				NOCP_CNT_ALARM_MODE_MIN_MAX_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(nocp->regmap, NOCP_COUNTERS_2_ALARM_MODE,
				NOCP_CNT_ALARM_MODE_MASK,
				NOCP_CNT_ALARM_MODE_MIN_MAX_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(nocp->regmap, NOCP_COUNTERS_3_ALARM_MODE,
				NOCP_CNT_ALARM_MODE_MASK,
				NOCP_CNT_ALARM_MODE_MIN_MAX_MASK);
	if (ret < 0)
		goto out;

	/* Enable the measurements by setting AlarmEn and StatEn */
	ret = regmap_update_bits(nocp->regmap, NOCP_MAIN_CTL,
			NOCP_MAIN_CTL_STATEN_MASK | NOCP_MAIN_CTL_ALARMEN_MASK,
			NOCP_MAIN_CTL_STATEN_MASK | NOCP_MAIN_CTL_ALARMEN_MASK);
	if (ret < 0)
		goto out;

	/* Set GlobalEN */
	ret = regmap_update_bits(nocp->regmap, NOCP_CFG_CTL,
				NOCP_CFG_CTL_GLOBALEN_MASK,
				NOCP_CFG_CTL_GLOBALEN_MASK);
	if (ret < 0)
		goto out;

	/* Enable NoC probe */
	ret = regmap_update_bits(nocp->regmap, NOCP_MAIN_CTL,
				NOCP_MAIN_CTL_STATEN_MASK,
				NOCP_MAIN_CTL_STATEN_MASK);
	if (ret < 0)
		goto out;

	return 0;

out:
	/* Reset NoC probe */
	if (regmap_update_bits(nocp->regmap, NOCP_MAIN_CTL,
				NOCP_MAIN_CTL_STATEN_MASK, 0)) {
		dev_err(nocp->dev, "Failed to reset NoC probe device\n");
	}

	return ret;
}

static int exynos_nocp_get_event(struct devfreq_event_dev *edev,
				struct devfreq_event_data *edata)
{
	struct exynos_nocp *nocp = devfreq_event_get_drvdata(edev);
	unsigned int counter[4];
	int ret;

	/* Read cycle count */
	ret = regmap_read(nocp->regmap, NOCP_COUNTERS_0_VAL, &counter[0]);
	if (ret < 0)
		goto out;

	ret = regmap_read(nocp->regmap, NOCP_COUNTERS_1_VAL, &counter[1]);
	if (ret < 0)
		goto out;

	ret = regmap_read(nocp->regmap, NOCP_COUNTERS_2_VAL, &counter[2]);
	if (ret < 0)
		goto out;

	ret = regmap_read(nocp->regmap, NOCP_COUNTERS_3_VAL, &counter[3]);
	if (ret < 0)
		goto out;

	edata->load_count = ((counter[1] << 16) | counter[0]);
	edata->total_count = ((counter[3] << 16) | counter[2]);

	dev_dbg(&edev->dev, "%s (event: %ld/%ld)\n", edev->desc->name,
					edata->load_count, edata->total_count);

	return 0;

out:
	dev_err(nocp->dev, "Failed to read the counter of NoC probe device\n");

	return ret;
}

static const struct devfreq_event_ops exynos_nocp_ops = {
	.set_event = exynos_nocp_set_event,
	.get_event = exynos_nocp_get_event,
};

static const struct of_device_id exynos_nocp_id_match[] = {
	{ .compatible = "samsung,exynos5420-nocp", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, exynos_nocp_id_match);

static struct regmap_config exynos_nocp_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = NOCP_COUNTERS_3_VAL,
};

static int exynos_nocp_parse_dt(struct platform_device *pdev,
				struct exynos_nocp *nocp)
{
	struct device *dev = nocp->dev;
	struct device_node *np = dev->of_node;
	struct resource *res;
	void __iomem *base;

	if (!np) {
		dev_err(dev, "failed to find devicetree node\n");
		return -EINVAL;
	}

	nocp->clk = devm_clk_get(dev, "nocp");
	if (IS_ERR(nocp->clk))
		nocp->clk = NULL;

	/* Maps the memory mapped IO to control nocp register */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	exynos_nocp_regmap_config.max_register = resource_size(res) - 4;

	nocp->regmap = devm_regmap_init_mmio(dev, base,
					&exynos_nocp_regmap_config);
	if (IS_ERR(nocp->regmap)) {
		dev_err(dev, "failed to initialize regmap\n");
		return PTR_ERR(nocp->regmap);
	}

	return 0;
}

static int exynos_nocp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct exynos_nocp *nocp;
	int ret;

	nocp = devm_kzalloc(&pdev->dev, sizeof(*nocp), GFP_KERNEL);
	if (!nocp)
		return -ENOMEM;

	nocp->dev = &pdev->dev;

	/* Parse dt data to get resource */
	ret = exynos_nocp_parse_dt(pdev, nocp);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"failed to parse devicetree for resource\n");
		return ret;
	}

	/* Add devfreq-event device to measure the bandwidth of NoC */
	nocp->desc.ops = &exynos_nocp_ops;
	nocp->desc.driver_data = nocp;
	nocp->desc.name = np->full_name;
	nocp->edev = devm_devfreq_event_add_edev(&pdev->dev, &nocp->desc);
	if (IS_ERR(nocp->edev)) {
		dev_err(&pdev->dev,
			"failed to add devfreq-event device\n");
		return PTR_ERR(nocp->edev);
	}
	platform_set_drvdata(pdev, nocp);

	ret = clk_prepare_enable(nocp->clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare ppmu clock\n");
		return ret;
	}

	pr_info("exynos-nocp: new NoC Probe device registered: %s\n",
			dev_name(dev));

	return 0;
}

static int exynos_nocp_remove(struct platform_device *pdev)
{
	struct exynos_nocp *nocp = platform_get_drvdata(pdev);

	clk_disable_unprepare(nocp->clk);

	return 0;
}

static struct platform_driver exynos_nocp_driver = {
	.probe	= exynos_nocp_probe,
	.remove	= exynos_nocp_remove,
	.driver = {
		.name	= "exynos-nocp",
		.of_match_table = exynos_nocp_id_match,
	},
};
module_platform_driver(exynos_nocp_driver);

MODULE_DESCRIPTION("Exynos NoC (Network on Chip) Probe driver");
MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_LICENSE("GPL");
