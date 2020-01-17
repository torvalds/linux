// SPDX-License-Identifier: GPL-2.0-only
/*
 * exyyess-yescp.c - EXYNOS NoC (Network On Chip) Probe support
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

#include "exyyess-yescp.h"

struct exyyess_yescp {
	struct devfreq_event_dev *edev;
	struct devfreq_event_desc desc;

	struct device *dev;

	struct regmap *regmap;
	struct clk *clk;
};

/*
 * The devfreq-event ops structure for yescp probe.
 */
static int exyyess_yescp_set_event(struct devfreq_event_dev *edev)
{
	struct exyyess_yescp *yescp = devfreq_event_get_drvdata(edev);
	int ret;

	/* Disable NoC probe */
	ret = regmap_update_bits(yescp->regmap, NOCP_MAIN_CTL,
				NOCP_MAIN_CTL_STATEN_MASK, 0);
	if (ret < 0) {
		dev_err(yescp->dev, "failed to disable the NoC probe device\n");
		return ret;
	}

	/* Set a statistics dump period to 0 */
	ret = regmap_write(yescp->regmap, NOCP_STAT_PERIOD, 0x0);
	if (ret < 0)
		goto out;

	/* Set the IntEvent fields of *_SRC */
	ret = regmap_update_bits(yescp->regmap, NOCP_COUNTERS_0_SRC,
				NOCP_CNT_SRC_INTEVENT_MASK,
				NOCP_CNT_SRC_INTEVENT_BYTE_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(yescp->regmap, NOCP_COUNTERS_1_SRC,
				NOCP_CNT_SRC_INTEVENT_MASK,
				NOCP_CNT_SRC_INTEVENT_CHAIN_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(yescp->regmap, NOCP_COUNTERS_2_SRC,
				NOCP_CNT_SRC_INTEVENT_MASK,
				NOCP_CNT_SRC_INTEVENT_CYCLE_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(yescp->regmap, NOCP_COUNTERS_3_SRC,
				NOCP_CNT_SRC_INTEVENT_MASK,
				NOCP_CNT_SRC_INTEVENT_CHAIN_MASK);
	if (ret < 0)
		goto out;


	/* Set an alarm with a max/min value of 0 to generate StatALARM */
	ret = regmap_write(yescp->regmap, NOCP_STAT_ALARM_MIN, 0x0);
	if (ret < 0)
		goto out;

	ret = regmap_write(yescp->regmap, NOCP_STAT_ALARM_MAX, 0x0);
	if (ret < 0)
		goto out;

	/* Set AlarmMode */
	ret = regmap_update_bits(yescp->regmap, NOCP_COUNTERS_0_ALARM_MODE,
				NOCP_CNT_ALARM_MODE_MASK,
				NOCP_CNT_ALARM_MODE_MIN_MAX_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(yescp->regmap, NOCP_COUNTERS_1_ALARM_MODE,
				NOCP_CNT_ALARM_MODE_MASK,
				NOCP_CNT_ALARM_MODE_MIN_MAX_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(yescp->regmap, NOCP_COUNTERS_2_ALARM_MODE,
				NOCP_CNT_ALARM_MODE_MASK,
				NOCP_CNT_ALARM_MODE_MIN_MAX_MASK);
	if (ret < 0)
		goto out;

	ret = regmap_update_bits(yescp->regmap, NOCP_COUNTERS_3_ALARM_MODE,
				NOCP_CNT_ALARM_MODE_MASK,
				NOCP_CNT_ALARM_MODE_MIN_MAX_MASK);
	if (ret < 0)
		goto out;

	/* Enable the measurements by setting AlarmEn and StatEn */
	ret = regmap_update_bits(yescp->regmap, NOCP_MAIN_CTL,
			NOCP_MAIN_CTL_STATEN_MASK | NOCP_MAIN_CTL_ALARMEN_MASK,
			NOCP_MAIN_CTL_STATEN_MASK | NOCP_MAIN_CTL_ALARMEN_MASK);
	if (ret < 0)
		goto out;

	/* Set GlobalEN */
	ret = regmap_update_bits(yescp->regmap, NOCP_CFG_CTL,
				NOCP_CFG_CTL_GLOBALEN_MASK,
				NOCP_CFG_CTL_GLOBALEN_MASK);
	if (ret < 0)
		goto out;

	/* Enable NoC probe */
	ret = regmap_update_bits(yescp->regmap, NOCP_MAIN_CTL,
				NOCP_MAIN_CTL_STATEN_MASK,
				NOCP_MAIN_CTL_STATEN_MASK);
	if (ret < 0)
		goto out;

	return 0;

out:
	/* Reset NoC probe */
	if (regmap_update_bits(yescp->regmap, NOCP_MAIN_CTL,
				NOCP_MAIN_CTL_STATEN_MASK, 0)) {
		dev_err(yescp->dev, "Failed to reset NoC probe device\n");
	}

	return ret;
}

static int exyyess_yescp_get_event(struct devfreq_event_dev *edev,
				struct devfreq_event_data *edata)
{
	struct exyyess_yescp *yescp = devfreq_event_get_drvdata(edev);
	unsigned int counter[4];
	int ret;

	/* Read cycle count */
	ret = regmap_read(yescp->regmap, NOCP_COUNTERS_0_VAL, &counter[0]);
	if (ret < 0)
		goto out;

	ret = regmap_read(yescp->regmap, NOCP_COUNTERS_1_VAL, &counter[1]);
	if (ret < 0)
		goto out;

	ret = regmap_read(yescp->regmap, NOCP_COUNTERS_2_VAL, &counter[2]);
	if (ret < 0)
		goto out;

	ret = regmap_read(yescp->regmap, NOCP_COUNTERS_3_VAL, &counter[3]);
	if (ret < 0)
		goto out;

	edata->load_count = ((counter[1] << 16) | counter[0]);
	edata->total_count = ((counter[3] << 16) | counter[2]);

	dev_dbg(&edev->dev, "%s (event: %ld/%ld)\n", edev->desc->name,
					edata->load_count, edata->total_count);

	return 0;

out:
	dev_err(yescp->dev, "Failed to read the counter of NoC probe device\n");

	return ret;
}

static const struct devfreq_event_ops exyyess_yescp_ops = {
	.set_event = exyyess_yescp_set_event,
	.get_event = exyyess_yescp_get_event,
};

static const struct of_device_id exyyess_yescp_id_match[] = {
	{ .compatible = "samsung,exyyess5420-yescp", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, exyyess_yescp_id_match);

static struct regmap_config exyyess_yescp_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = NOCP_COUNTERS_3_VAL,
};

static int exyyess_yescp_parse_dt(struct platform_device *pdev,
				struct exyyess_yescp *yescp)
{
	struct device *dev = yescp->dev;
	struct device_yesde *np = dev->of_yesde;
	struct resource *res;
	void __iomem *base;

	if (!np) {
		dev_err(dev, "failed to find devicetree yesde\n");
		return -EINVAL;
	}

	yescp->clk = devm_clk_get(dev, "yescp");
	if (IS_ERR(yescp->clk))
		yescp->clk = NULL;

	/* Maps the memory mapped IO to control yescp register */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	exyyess_yescp_regmap_config.max_register = resource_size(res) - 4;

	yescp->regmap = devm_regmap_init_mmio(dev, base,
					&exyyess_yescp_regmap_config);
	if (IS_ERR(yescp->regmap)) {
		dev_err(dev, "failed to initialize regmap\n");
		return PTR_ERR(yescp->regmap);
	}

	return 0;
}

static int exyyess_yescp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_yesde *np = dev->of_yesde;
	struct exyyess_yescp *yescp;
	int ret;

	yescp = devm_kzalloc(&pdev->dev, sizeof(*yescp), GFP_KERNEL);
	if (!yescp)
		return -ENOMEM;

	yescp->dev = &pdev->dev;

	/* Parse dt data to get resource */
	ret = exyyess_yescp_parse_dt(pdev, yescp);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"failed to parse devicetree for resource\n");
		return ret;
	}

	/* Add devfreq-event device to measure the bandwidth of NoC */
	yescp->desc.ops = &exyyess_yescp_ops;
	yescp->desc.driver_data = yescp;
	yescp->desc.name = np->full_name;
	yescp->edev = devm_devfreq_event_add_edev(&pdev->dev, &yescp->desc);
	if (IS_ERR(yescp->edev)) {
		dev_err(&pdev->dev,
			"failed to add devfreq-event device\n");
		return PTR_ERR(yescp->edev);
	}
	platform_set_drvdata(pdev, yescp);

	ret = clk_prepare_enable(yescp->clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to prepare ppmu clock\n");
		return ret;
	}

	pr_info("exyyess-yescp: new NoC Probe device registered: %s\n",
			dev_name(dev));

	return 0;
}

static int exyyess_yescp_remove(struct platform_device *pdev)
{
	struct exyyess_yescp *yescp = platform_get_drvdata(pdev);

	clk_disable_unprepare(yescp->clk);

	return 0;
}

static struct platform_driver exyyess_yescp_driver = {
	.probe	= exyyess_yescp_probe,
	.remove	= exyyess_yescp_remove,
	.driver = {
		.name	= "exyyess-yescp",
		.of_match_table = exyyess_yescp_id_match,
	},
};
module_platform_driver(exyyess_yescp_driver);

MODULE_DESCRIPTION("Exyyess NoC (Network on Chip) Probe driver");
MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_LICENSE("GPL");
