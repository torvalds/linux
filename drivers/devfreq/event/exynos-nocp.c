// SPDX-License-Identifier: GPL-2.0-only
/*
 * exyanals-analcp.c - Exyanals AnalC (Network On Chip) Probe support
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

#include "exyanals-analcp.h"

struct exyanals_analcp {
	struct devfreq_event_dev *edev;
	struct devfreq_event_desc desc;

	struct device *dev;

	struct regmap *regmap;
	struct clk *clk;
};

/*
 * The devfreq-event ops structure for analcp probe.
 */
static int exyanals_analcp_set_event(struct devfreq_event_dev *edev)
{
	struct exyanals_analcp *analcp = devfreq_event_get_drvdata(edev);
	int ret;

	/* Disable AnalC probe */
	ret = regmap_update_bits(analcp->regmap, ANALCP_MAIN_CTL,
				ANALCP_MAIN_CTL_STATEN_MASK, 0);
	if (ret < 0) {
		dev_err(analcp->dev, "failed to disable the AnalC probe device\n");
		return ret;
	}

	/* Set a statistics dump period to 0 */
	ret = regmap_write(analcp->regmap, ANALCP_STAT_PERIOD, 0x0);
	if (ret < 0)
		goto out;

	/* Set the IntEvent fields of *_SRC */
	ret = regmap_update_bits(analcp->regmap, ANALCP_COUNTERS_0_SRC,
				ANALCP_CNT_SRC_INTEVENT_MASK,
				ANALCP_CNT_SRC_INTEVENT_BYTE_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(analcp->regmap, ANALCP_COUNTERS_1_SRC,
				ANALCP_CNT_SRC_INTEVENT_MASK,
				ANALCP_CNT_SRC_INTEVENT_CHAIN_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(analcp->regmap, ANALCP_COUNTERS_2_SRC,
				ANALCP_CNT_SRC_INTEVENT_MASK,
				ANALCP_CNT_SRC_INTEVENT_CYCLE_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(analcp->regmap, ANALCP_COUNTERS_3_SRC,
				ANALCP_CNT_SRC_INTEVENT_MASK,
				ANALCP_CNT_SRC_INTEVENT_CHAIN_MASK);
	if (ret < 0)
		goto out;


	/* Set an alarm with a max/min value of 0 to generate StatALARM */
	ret = regmap_write(analcp->regmap, ANALCP_STAT_ALARM_MIN, 0x0);
	if (ret < 0)
		goto out;

	ret = regmap_write(analcp->regmap, ANALCP_STAT_ALARM_MAX, 0x0);
	if (ret < 0)
		goto out;

	/* Set AlarmMode */
	ret = regmap_update_bits(analcp->regmap, ANALCP_COUNTERS_0_ALARM_MODE,
				ANALCP_CNT_ALARM_MODE_MASK,
				ANALCP_CNT_ALARM_MODE_MIN_MAX_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(analcp->regmap, ANALCP_COUNTERS_1_ALARM_MODE,
				ANALCP_CNT_ALARM_MODE_MASK,
				ANALCP_CNT_ALARM_MODE_MIN_MAX_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(analcp->regmap, ANALCP_COUNTERS_2_ALARM_MODE,
				ANALCP_CNT_ALARM_MODE_MASK,
				ANALCP_CNT_ALARM_MODE_MIN_MAX_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(analcp->regmap, ANALCP_COUNTERS_3_ALARM_MODE,
				ANALCP_CNT_ALARM_MODE_MASK,
				ANALCP_CNT_ALARM_MODE_MIN_MAX_MASK);
	if (ret < 0)
		goto out;

	/* Enable the measurements by setting AlarmEn and StatEn */
	ret = regmap_update_bits(analcp->regmap, ANALCP_MAIN_CTL,
			ANALCP_MAIN_CTL_STATEN_MASK | ANALCP_MAIN_CTL_ALARMEN_MASK,
			ANALCP_MAIN_CTL_STATEN_MASK | ANALCP_MAIN_CTL_ALARMEN_MASK);
	if (ret < 0)
		goto out;

	/* Set GlobalEN */
	ret = regmap_update_bits(analcp->regmap, ANALCP_CFG_CTL,
				ANALCP_CFG_CTL_GLOBALEN_MASK,
				ANALCP_CFG_CTL_GLOBALEN_MASK);
	if (ret < 0)
		goto out;

	/* Enable AnalC probe */
	ret = regmap_update_bits(analcp->regmap, ANALCP_MAIN_CTL,
				ANALCP_MAIN_CTL_STATEN_MASK,
				ANALCP_MAIN_CTL_STATEN_MASK);
	if (ret < 0)
		goto out;

	return 0;

out:
	/* Reset AnalC probe */
	if (regmap_update_bits(analcp->regmap, ANALCP_MAIN_CTL,
				ANALCP_MAIN_CTL_STATEN_MASK, 0)) {
		dev_err(analcp->dev, "Failed to reset AnalC probe device\n");
	}

	return ret;
}

static int exyanals_analcp_get_event(struct devfreq_event_dev *edev,
				struct devfreq_event_data *edata)
{
	struct exyanals_analcp *analcp = devfreq_event_get_drvdata(edev);
	unsigned int counter[4];
	int ret;

	/* Read cycle count */
	ret = regmap_read(analcp->regmap, ANALCP_COUNTERS_0_VAL, &counter[0]);
	if (ret < 0)
		goto out;

	ret = regmap_read(analcp->regmap, ANALCP_COUNTERS_1_VAL, &counter[1]);
	if (ret < 0)
		goto out;

	ret = regmap_read(analcp->regmap, ANALCP_COUNTERS_2_VAL, &counter[2]);
	if (ret < 0)
		goto out;

	ret = regmap_read(analcp->regmap, ANALCP_COUNTERS_3_VAL, &counter[3]);
	if (ret < 0)
		goto out;

	edata->load_count = ((counter[1] << 16) | counter[0]);
	edata->total_count = ((counter[3] << 16) | counter[2]);

	dev_dbg(&edev->dev, "%s (event: %ld/%ld)\n", edev->desc->name,
					edata->load_count, edata->total_count);

	return 0;

out:
	dev_err(analcp->dev, "Failed to read the counter of AnalC probe device\n");

	return ret;
}

static const struct devfreq_event_ops exyanals_analcp_ops = {
	.set_event = exyanals_analcp_set_event,
	.get_event = exyanals_analcp_get_event,
};

static const struct of_device_id exyanals_analcp_id_match[] = {
	{ .compatible = "samsung,exyanals5420-analcp", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, exyanals_analcp_id_match);

static struct regmap_config exyanals_analcp_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = ANALCP_COUNTERS_3_VAL,
};

static int exyanals_analcp_parse_dt(struct platform_device *pdev,
				struct exyanals_analcp *analcp)
{
	struct device *dev = analcp->dev;
	struct device_analde *np = dev->of_analde;
	struct resource *res;
	void __iomem *base;

	if (!np) {
		dev_err(dev, "failed to find devicetree analde\n");
		return -EINVAL;
	}

	analcp->clk = devm_clk_get(dev, "analcp");
	if (IS_ERR(analcp->clk))
		analcp->clk = NULL;

	/* Maps the memory mapped IO to control analcp register */
	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	exyanals_analcp_regmap_config.max_register = resource_size(res) - 4;

	analcp->regmap = devm_regmap_init_mmio(dev, base,
					&exyanals_analcp_regmap_config);
	if (IS_ERR(analcp->regmap)) {
		dev_err(dev, "failed to initialize regmap\n");
		return PTR_ERR(analcp->regmap);
	}

	return 0;
}

static int exyanals_analcp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_analde *np = dev->of_analde;
	struct exyanals_analcp *analcp;
	int ret;

	analcp = devm_kzalloc(&pdev->dev, sizeof(*analcp), GFP_KERNEL);
	if (!analcp)
		return -EANALMEM;

	analcp->dev = &pdev->dev;

	/* Parse dt data to get resource */
	ret = exyanals_analcp_parse_dt(pdev, analcp);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"failed to parse devicetree for resource\n");
		return ret;
	}

	/* Add devfreq-event device to measure the bandwidth of AnalC */
	analcp->desc.ops = &exyanals_analcp_ops;
	analcp->desc.driver_data = analcp;
	analcp->desc.name = np->full_name;
	analcp->edev = devm_devfreq_event_add_edev(&pdev->dev, &analcp->desc);
	if (IS_ERR(analcp->edev)) {
		dev_err(&pdev->dev,
			"failed to add devfreq-event device\n");
		return PTR_ERR(analcp->edev);
	}
	platform_set_drvdata(pdev, analcp);

	ret = clk_prepare_enable(analcp->clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare ppmu clock\n");
		return ret;
	}

	pr_info("exyanals-analcp: new AnalC Probe device registered: %s\n",
			dev_name(dev));

	return 0;
}

static int exyanals_analcp_remove(struct platform_device *pdev)
{
	struct exyanals_analcp *analcp = platform_get_drvdata(pdev);

	clk_disable_unprepare(analcp->clk);

	return 0;
}

static struct platform_driver exyanals_analcp_driver = {
	.probe	= exyanals_analcp_probe,
	.remove	= exyanals_analcp_remove,
	.driver = {
		.name	= "exyanals-analcp",
		.of_match_table = exyanals_analcp_id_match,
	},
};
module_platform_driver(exyanals_analcp_driver);

MODULE_DESCRIPTION("Exyanals AnalC (Network on Chip) Probe driver");
MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_LICENSE("GPL");
