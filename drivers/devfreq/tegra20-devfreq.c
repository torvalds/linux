// SPDX-License-Identifier: GPL-2.0
/*
 * NVIDIA Tegra20 devfreq driver
 *
 * Copyright (C) 2019 GRATE-DRIVER project
 */

#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>

#include <soc/tegra/mc.h>

#include "governor.h"

#define MC_STAT_CONTROL				0x90
#define MC_STAT_EMC_CLOCK_LIMIT			0xa0
#define MC_STAT_EMC_CLOCKS			0xa4
#define MC_STAT_EMC_CONTROL			0xa8
#define MC_STAT_EMC_COUNT			0xb8

#define EMC_GATHER_CLEAR			(1 << 8)
#define EMC_GATHER_ENABLE			(3 << 8)

struct tegra_devfreq {
	struct devfreq *devfreq;
	struct clk *emc_clock;
	void __iomem *regs;
};

static int tegra_devfreq_target(struct device *dev, unsigned long *freq,
				u32 flags)
{
	struct tegra_devfreq *tegra = dev_get_drvdata(dev);
	struct devfreq *devfreq = tegra->devfreq;
	struct dev_pm_opp *opp;
	unsigned long rate;
	int err;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	rate = dev_pm_opp_get_freq(opp);
	dev_pm_opp_put(opp);

	err = clk_set_min_rate(tegra->emc_clock, rate);
	if (err)
		return err;

	err = clk_set_rate(tegra->emc_clock, 0);
	if (err)
		goto restore_min_rate;

	return 0;

restore_min_rate:
	clk_set_min_rate(tegra->emc_clock, devfreq->previous_freq);

	return err;
}

static int tegra_devfreq_get_dev_status(struct device *dev,
					struct devfreq_dev_status *stat)
{
	struct tegra_devfreq *tegra = dev_get_drvdata(dev);

	/*
	 * EMC_COUNT returns number of memory events, that number is lower
	 * than the number of clocks. Conversion ratio of 1/8 results in a
	 * bit higher bandwidth than actually needed, it is good enough for
	 * the time being because drivers don't support requesting minimum
	 * needed memory bandwidth yet.
	 *
	 * TODO: adjust the ratio value once relevant drivers will support
	 * memory bandwidth management.
	 */
	stat->busy_time = readl_relaxed(tegra->regs + MC_STAT_EMC_COUNT);
	stat->total_time = readl_relaxed(tegra->regs + MC_STAT_EMC_CLOCKS) / 8;
	stat->current_frequency = clk_get_rate(tegra->emc_clock);

	writel_relaxed(EMC_GATHER_CLEAR, tegra->regs + MC_STAT_CONTROL);
	writel_relaxed(EMC_GATHER_ENABLE, tegra->regs + MC_STAT_CONTROL);

	return 0;
}

static struct devfreq_dev_profile tegra_devfreq_profile = {
	.polling_ms	= 500,
	.target		= tegra_devfreq_target,
	.get_dev_status	= tegra_devfreq_get_dev_status,
};

static struct tegra_mc *tegra_get_memory_controller(void)
{
	struct platform_device *pdev;
	struct device_node *np;
	struct tegra_mc *mc;

	np = of_find_compatible_node(NULL, NULL, "nvidia,tegra20-mc-gart");
	if (!np)
		return ERR_PTR(-ENOENT);

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return ERR_PTR(-ENODEV);

	mc = platform_get_drvdata(pdev);
	if (!mc)
		return ERR_PTR(-EPROBE_DEFER);

	return mc;
}

static int tegra_devfreq_probe(struct platform_device *pdev)
{
	struct tegra_devfreq *tegra;
	struct tegra_mc *mc;
	unsigned long max_rate;
	unsigned long rate;
	int err;

	mc = tegra_get_memory_controller();
	if (IS_ERR(mc)) {
		err = PTR_ERR(mc);
		dev_err(&pdev->dev, "failed to get memory controller: %d\n",
			err);
		return err;
	}

	tegra = devm_kzalloc(&pdev->dev, sizeof(*tegra), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	/* EMC is a system-critical clock that is always enabled */
	tegra->emc_clock = devm_clk_get(&pdev->dev, "emc");
	if (IS_ERR(tegra->emc_clock)) {
		err = PTR_ERR(tegra->emc_clock);
		dev_err(&pdev->dev, "failed to get emc clock: %d\n", err);
		return err;
	}

	tegra->regs = mc->regs;

	max_rate = clk_round_rate(tegra->emc_clock, ULONG_MAX);

	for (rate = 0; rate <= max_rate; rate++) {
		rate = clk_round_rate(tegra->emc_clock, rate);

		err = dev_pm_opp_add(&pdev->dev, rate, 0);
		if (err) {
			dev_err(&pdev->dev, "failed to add opp: %d\n", err);
			goto remove_opps;
		}
	}

	/*
	 * Reset statistic gathers state, select global bandwidth for the
	 * statistics collection mode and set clocks counter saturation
	 * limit to maximum.
	 */
	writel_relaxed(0x00000000, tegra->regs + MC_STAT_CONTROL);
	writel_relaxed(0x00000000, tegra->regs + MC_STAT_EMC_CONTROL);
	writel_relaxed(0xffffffff, tegra->regs + MC_STAT_EMC_CLOCK_LIMIT);

	platform_set_drvdata(pdev, tegra);

	tegra->devfreq = devfreq_add_device(&pdev->dev, &tegra_devfreq_profile,
					    DEVFREQ_GOV_SIMPLE_ONDEMAND, NULL);
	if (IS_ERR(tegra->devfreq)) {
		err = PTR_ERR(tegra->devfreq);
		goto remove_opps;
	}

	return 0;

remove_opps:
	dev_pm_opp_remove_all_dynamic(&pdev->dev);

	return err;
}

static int tegra_devfreq_remove(struct platform_device *pdev)
{
	struct tegra_devfreq *tegra = platform_get_drvdata(pdev);

	devfreq_remove_device(tegra->devfreq);
	dev_pm_opp_remove_all_dynamic(&pdev->dev);

	return 0;
}

static struct platform_driver tegra_devfreq_driver = {
	.probe		= tegra_devfreq_probe,
	.remove		= tegra_devfreq_remove,
	.driver		= {
		.name	= "tegra20-devfreq",
	},
};
module_platform_driver(tegra_devfreq_driver);

MODULE_ALIAS("platform:tegra20-devfreq");
MODULE_AUTHOR("Dmitry Osipenko <digetx@gmail.com>");
MODULE_DESCRIPTION("NVIDIA Tegra20 devfreq driver");
MODULE_LICENSE("GPL v2");
