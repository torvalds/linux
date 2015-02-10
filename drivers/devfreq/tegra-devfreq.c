/*
 * A devfreq driver for NVIDIA Tegra SoCs
 *
 * Copyright (c) 2014 NVIDIA CORPORATION. All rights reserved.
 * Copyright (C) 2014 Google, Inc
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/devfreq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/reset.h>

#include "governor.h"

#define ACTMON_GLB_STATUS					0x0
#define ACTMON_GLB_PERIOD_CTRL					0x4

#define ACTMON_DEV_CTRL						0x0
#define ACTMON_DEV_CTRL_K_VAL_SHIFT				10
#define ACTMON_DEV_CTRL_ENB_PERIODIC				BIT(18)
#define ACTMON_DEV_CTRL_AVG_BELOW_WMARK_EN			BIT(20)
#define ACTMON_DEV_CTRL_AVG_ABOVE_WMARK_EN			BIT(21)
#define ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_NUM_SHIFT	23
#define ACTMON_DEV_CTRL_CONSECUTIVE_ABOVE_WMARK_NUM_SHIFT	26
#define ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_EN		BIT(29)
#define ACTMON_DEV_CTRL_CONSECUTIVE_ABOVE_WMARK_EN		BIT(30)
#define ACTMON_DEV_CTRL_ENB					BIT(31)

#define ACTMON_DEV_UPPER_WMARK					0x4
#define ACTMON_DEV_LOWER_WMARK					0x8
#define ACTMON_DEV_INIT_AVG					0xc
#define ACTMON_DEV_AVG_UPPER_WMARK				0x10
#define ACTMON_DEV_AVG_LOWER_WMARK				0x14
#define ACTMON_DEV_COUNT_WEIGHT					0x18
#define ACTMON_DEV_AVG_COUNT					0x20
#define ACTMON_DEV_INTR_STATUS					0x24

#define ACTMON_INTR_STATUS_CLEAR				0xffffffff

#define ACTMON_DEV_INTR_CONSECUTIVE_UPPER			BIT(31)
#define ACTMON_DEV_INTR_CONSECUTIVE_LOWER			BIT(30)

#define ACTMON_ABOVE_WMARK_WINDOW				1
#define ACTMON_BELOW_WMARK_WINDOW				3
#define ACTMON_BOOST_FREQ_STEP					16000

/* activity counter is incremented every 256 memory transactions, and each
 * transaction takes 4 EMC clocks for Tegra124; So the COUNT_WEIGHT is
 * 4 * 256 = 1024.
 */
#define ACTMON_COUNT_WEIGHT					0x400

/*
 * ACTMON_AVERAGE_WINDOW_LOG2: default value for @DEV_CTRL_K_VAL, which
 * translates to 2 ^ (K_VAL + 1). ex: 2 ^ (6 + 1) = 128
 */
#define ACTMON_AVERAGE_WINDOW_LOG2			6
#define ACTMON_SAMPLING_PERIOD				12 /* ms */
#define ACTMON_DEFAULT_AVG_BAND				6  /* 1/10 of % */

#define KHZ							1000

/* Assume that the bus is saturated if the utilization is 25% */
#define BUS_SATURATION_RATIO					25

/**
 * struct tegra_devfreq_device_config - configuration specific to an ACTMON
 * device
 *
 * Coefficients and thresholds are in %
 */
struct tegra_devfreq_device_config {
	u32		offset;
	u32		irq_mask;

	unsigned int	boost_up_coeff;
	unsigned int	boost_down_coeff;
	unsigned int	boost_up_threshold;
	unsigned int	boost_down_threshold;
	u32		avg_dependency_threshold;
};

enum tegra_actmon_device {
	MCALL = 0,
	MCCPU,
};

static struct tegra_devfreq_device_config actmon_device_configs[] = {
	{
		/* MCALL */
		.offset = 0x1c0,
		.irq_mask = 1 << 26,
		.boost_up_coeff = 200,
		.boost_down_coeff = 50,
		.boost_up_threshold = 60,
		.boost_down_threshold = 40,
	},
	{
		/* MCCPU */
		.offset = 0x200,
		.irq_mask = 1 << 25,
		.boost_up_coeff = 800,
		.boost_down_coeff = 90,
		.boost_up_threshold = 27,
		.boost_down_threshold = 10,
		.avg_dependency_threshold = 50000,
	},
};

/**
 * struct tegra_devfreq_device - state specific to an ACTMON device
 *
 * Frequencies are in kHz.
 */
struct tegra_devfreq_device {
	const struct tegra_devfreq_device_config *config;

	void __iomem	*regs;
	u32		avg_band_freq;
	u32		avg_count;

	unsigned long	target_freq;
	unsigned long	boost_freq;
};

struct tegra_devfreq {
	struct devfreq		*devfreq;

	struct platform_device	*pdev;
	struct reset_control	*reset;
	struct clk		*clock;
	void __iomem		*regs;

	spinlock_t		lock;

	struct clk		*emc_clock;
	unsigned long		max_freq;
	unsigned long		cur_freq;
	struct notifier_block	rate_change_nb;

	struct tegra_devfreq_device devices[ARRAY_SIZE(actmon_device_configs)];
};

struct tegra_actmon_emc_ratio {
	unsigned long cpu_freq;
	unsigned long emc_freq;
};

static struct tegra_actmon_emc_ratio actmon_emc_ratios[] = {
	{ 1400000, ULONG_MAX },
	{ 1200000,    750000 },
	{ 1100000,    600000 },
	{ 1000000,    500000 },
	{  800000,    375000 },
	{  500000,    200000 },
	{  250000,    100000 },
};

static unsigned long do_percent(unsigned long val, unsigned int pct)
{
	return val * pct / 100;
}

static void tegra_devfreq_update_avg_wmark(struct tegra_devfreq_device *dev)
{
	u32 avg = dev->avg_count;
	u32 band = dev->avg_band_freq * ACTMON_SAMPLING_PERIOD;

	writel(avg + band, dev->regs + ACTMON_DEV_AVG_UPPER_WMARK);
	avg = max(avg, band);
	writel(avg - band, dev->regs + ACTMON_DEV_AVG_LOWER_WMARK);
}

static void tegra_devfreq_update_wmark(struct tegra_devfreq *tegra,
				       struct tegra_devfreq_device *dev)
{
	u32 val = tegra->cur_freq * ACTMON_SAMPLING_PERIOD;

	writel(do_percent(val, dev->config->boost_up_threshold),
	       dev->regs + ACTMON_DEV_UPPER_WMARK);

	writel(do_percent(val, dev->config->boost_down_threshold),
	       dev->regs + ACTMON_DEV_LOWER_WMARK);
}

static void actmon_write_barrier(struct tegra_devfreq *tegra)
{
	/* ensure the update has reached the ACTMON */
	wmb();
	readl(tegra->regs + ACTMON_GLB_STATUS);
}

static irqreturn_t actmon_isr(int irq, void *data)
{
	struct tegra_devfreq *tegra = data;
	struct tegra_devfreq_device *dev = NULL;
	unsigned long flags;
	u32 val;
	unsigned int i;

	val = readl(tegra->regs + ACTMON_GLB_STATUS);

	for (i = 0; i < ARRAY_SIZE(tegra->devices); i++) {
		if (val & tegra->devices[i].config->irq_mask) {
			dev = tegra->devices + i;
			break;
		}
	}

	if (!dev)
		return IRQ_NONE;

	spin_lock_irqsave(&tegra->lock, flags);

	dev->avg_count = readl(dev->regs + ACTMON_DEV_AVG_COUNT);
	tegra_devfreq_update_avg_wmark(dev);

	val = readl(dev->regs + ACTMON_DEV_INTR_STATUS);
	if (val & ACTMON_DEV_INTR_CONSECUTIVE_UPPER) {
		val = readl(dev->regs + ACTMON_DEV_CTRL) |
			ACTMON_DEV_CTRL_CONSECUTIVE_ABOVE_WMARK_EN |
			ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_EN;

		/*
		 * new_boost = min(old_boost * up_coef + step, max_freq)
		 */
		dev->boost_freq = do_percent(dev->boost_freq,
					     dev->config->boost_up_coeff);
		dev->boost_freq += ACTMON_BOOST_FREQ_STEP;
		if (dev->boost_freq >= tegra->max_freq) {
			dev->boost_freq = tegra->max_freq;
			val &= ~ACTMON_DEV_CTRL_CONSECUTIVE_ABOVE_WMARK_EN;
		}
		writel(val, dev->regs + ACTMON_DEV_CTRL);
	} else if (val & ACTMON_DEV_INTR_CONSECUTIVE_LOWER) {
		val = readl(dev->regs + ACTMON_DEV_CTRL) |
			ACTMON_DEV_CTRL_CONSECUTIVE_ABOVE_WMARK_EN |
			ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_EN;

		/*
		 * new_boost = old_boost * down_coef
		 * or 0 if (old_boost * down_coef < step / 2)
		 */
		dev->boost_freq = do_percent(dev->boost_freq,
					     dev->config->boost_down_coeff);
		if (dev->boost_freq < (ACTMON_BOOST_FREQ_STEP >> 1)) {
			dev->boost_freq = 0;
			val &= ~ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_EN;
		}
		writel(val, dev->regs + ACTMON_DEV_CTRL);
	}

	if (dev->config->avg_dependency_threshold) {
		val = readl(dev->regs + ACTMON_DEV_CTRL);
		if (dev->avg_count >= dev->config->avg_dependency_threshold)
			val |= ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_EN;
		else if (dev->boost_freq == 0)
			val &= ~ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_EN;
		writel(val, dev->regs + ACTMON_DEV_CTRL);
	}

	writel(ACTMON_INTR_STATUS_CLEAR, dev->regs + ACTMON_DEV_INTR_STATUS);

	actmon_write_barrier(tegra);

	spin_unlock_irqrestore(&tegra->lock, flags);

	return IRQ_WAKE_THREAD;
}

static unsigned long actmon_cpu_to_emc_rate(struct tegra_devfreq *tegra,
					    unsigned long cpu_freq)
{
	unsigned int i;
	struct tegra_actmon_emc_ratio *ratio = actmon_emc_ratios;

	for (i = 0; i < ARRAY_SIZE(actmon_emc_ratios); i++, ratio++) {
		if (cpu_freq >= ratio->cpu_freq) {
			if (ratio->emc_freq >= tegra->max_freq)
				return tegra->max_freq;
			else
				return ratio->emc_freq;
		}
	}

	return 0;
}

static void actmon_update_target(struct tegra_devfreq *tegra,
				 struct tegra_devfreq_device *dev)
{
	unsigned long cpu_freq = 0;
	unsigned long static_cpu_emc_freq = 0;
	unsigned int avg_sustain_coef;
	unsigned long flags;

	if (dev->config->avg_dependency_threshold) {
		cpu_freq = cpufreq_get(0);
		static_cpu_emc_freq = actmon_cpu_to_emc_rate(tegra, cpu_freq);
	}

	spin_lock_irqsave(&tegra->lock, flags);

	dev->target_freq = dev->avg_count / ACTMON_SAMPLING_PERIOD;
	avg_sustain_coef = 100 * 100 / dev->config->boost_up_threshold;
	dev->target_freq = do_percent(dev->target_freq, avg_sustain_coef);
	dev->target_freq += dev->boost_freq;

	if (dev->avg_count >= dev->config->avg_dependency_threshold)
		dev->target_freq = max(dev->target_freq, static_cpu_emc_freq);

	spin_unlock_irqrestore(&tegra->lock, flags);
}

static irqreturn_t actmon_thread_isr(int irq, void *data)
{
	struct tegra_devfreq *tegra = data;

	mutex_lock(&tegra->devfreq->lock);
	update_devfreq(tegra->devfreq);
	mutex_unlock(&tegra->devfreq->lock);

	return IRQ_HANDLED;
}

static int tegra_actmon_rate_notify_cb(struct notifier_block *nb,
				       unsigned long action, void *ptr)
{
	struct clk_notifier_data *data = ptr;
	struct tegra_devfreq *tegra = container_of(nb, struct tegra_devfreq,
						   rate_change_nb);
	unsigned int i;
	unsigned long flags;

	spin_lock_irqsave(&tegra->lock, flags);

	switch (action) {
	case POST_RATE_CHANGE:
		tegra->cur_freq = data->new_rate / KHZ;

		for (i = 0; i < ARRAY_SIZE(tegra->devices); i++)
			tegra_devfreq_update_wmark(tegra, tegra->devices + i);

		actmon_write_barrier(tegra);
		break;
	case PRE_RATE_CHANGE:
		/* fall through */
	case ABORT_RATE_CHANGE:
		break;
	};

	spin_unlock_irqrestore(&tegra->lock, flags);

	return NOTIFY_OK;
}

static void tegra_actmon_configure_device(struct tegra_devfreq *tegra,
					  struct tegra_devfreq_device *dev)
{
	u32 val;

	dev->avg_band_freq = tegra->max_freq * ACTMON_DEFAULT_AVG_BAND / KHZ;
	dev->target_freq = tegra->cur_freq;

	dev->avg_count = tegra->cur_freq * ACTMON_SAMPLING_PERIOD;
	writel(dev->avg_count, dev->regs + ACTMON_DEV_INIT_AVG);

	tegra_devfreq_update_avg_wmark(dev);
	tegra_devfreq_update_wmark(tegra, dev);

	writel(ACTMON_COUNT_WEIGHT, dev->regs + ACTMON_DEV_COUNT_WEIGHT);
	writel(ACTMON_INTR_STATUS_CLEAR, dev->regs + ACTMON_DEV_INTR_STATUS);

	val = 0;
	val |= ACTMON_DEV_CTRL_ENB_PERIODIC |
	       ACTMON_DEV_CTRL_AVG_ABOVE_WMARK_EN |
	       ACTMON_DEV_CTRL_AVG_BELOW_WMARK_EN;
	val |= (ACTMON_AVERAGE_WINDOW_LOG2 - 1)
		<< ACTMON_DEV_CTRL_K_VAL_SHIFT;
	val |= (ACTMON_BELOW_WMARK_WINDOW - 1)
		<< ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_NUM_SHIFT;
	val |= (ACTMON_ABOVE_WMARK_WINDOW - 1)
		<< ACTMON_DEV_CTRL_CONSECUTIVE_ABOVE_WMARK_NUM_SHIFT;
	val |= ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_EN |
	       ACTMON_DEV_CTRL_CONSECUTIVE_ABOVE_WMARK_EN;

	writel(val, dev->regs + ACTMON_DEV_CTRL);

	actmon_write_barrier(tegra);

	val = readl(dev->regs + ACTMON_DEV_CTRL);
	val |= ACTMON_DEV_CTRL_ENB;
	writel(val, dev->regs + ACTMON_DEV_CTRL);

	actmon_write_barrier(tegra);
}

static int tegra_devfreq_suspend(struct device *dev)
{
	struct platform_device *pdev;
	struct tegra_devfreq *tegra;
	struct tegra_devfreq_device *actmon_dev;
	unsigned int i;
	u32 val;

	pdev = container_of(dev, struct platform_device, dev);
	tegra = platform_get_drvdata(pdev);

	for (i = 0; i < ARRAY_SIZE(tegra->devices); i++) {
		actmon_dev = &tegra->devices[i];

		val = readl(actmon_dev->regs + ACTMON_DEV_CTRL);
		val &= ~ACTMON_DEV_CTRL_ENB;
		writel(val, actmon_dev->regs + ACTMON_DEV_CTRL);

		writel(ACTMON_INTR_STATUS_CLEAR,
		       actmon_dev->regs + ACTMON_DEV_INTR_STATUS);

		actmon_write_barrier(tegra);
	}

	return 0;
}

static int tegra_devfreq_resume(struct device *dev)
{
	struct platform_device *pdev;
	struct tegra_devfreq *tegra;
	struct tegra_devfreq_device *actmon_dev;
	unsigned int i;

	pdev = container_of(dev, struct platform_device, dev);
	tegra = platform_get_drvdata(pdev);

	for (i = 0; i < ARRAY_SIZE(tegra->devices); i++) {
		actmon_dev = &tegra->devices[i];

		tegra_actmon_configure_device(tegra, actmon_dev);
	}

	return 0;
}

static int tegra_devfreq_target(struct device *dev, unsigned long *freq,
				u32 flags)
{
	struct platform_device *pdev;
	struct tegra_devfreq *tegra;
	struct dev_pm_opp *opp;
	unsigned long rate = *freq * KHZ;

	pdev = container_of(dev, struct platform_device, dev);
	tegra = platform_get_drvdata(pdev);

	rcu_read_lock();
	opp = devfreq_recommended_opp(dev, &rate, flags);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(dev, "Failed to find opp for %lu KHz\n", *freq);
		return PTR_ERR(opp);
	}
	rate = dev_pm_opp_get_freq(opp);
	rcu_read_unlock();

	/* TODO: Once we have per-user clk constraints, set a floor */
	clk_set_rate(tegra->emc_clock, rate);

	/* TODO: Set voltage as well */

	return 0;
}

static int tegra_devfreq_get_dev_status(struct device *dev,
					struct devfreq_dev_status *stat)
{
	struct platform_device *pdev;
	struct tegra_devfreq *tegra;
	struct tegra_devfreq_device *actmon_dev;

	pdev = container_of(dev, struct platform_device, dev);
	tegra = platform_get_drvdata(pdev);

	stat->current_frequency = tegra->cur_freq;

	/* To be used by the tegra governor */
	stat->private_data = tegra;

	/* The below are to be used by the other governors */

	actmon_dev = &tegra->devices[MCALL];

	/* Number of cycles spent on memory access */
	stat->busy_time = actmon_dev->avg_count;

	/* The bus can be considered to be saturated way before 100% */
	stat->busy_time *= 100 / BUS_SATURATION_RATIO;

	/* Number of cycles in a sampling period */
	stat->total_time = ACTMON_SAMPLING_PERIOD * tegra->cur_freq;

	return 0;
}

static int tegra_devfreq_get_target(struct devfreq *devfreq,
				    unsigned long *freq)
{
	struct devfreq_dev_status stat;
	struct tegra_devfreq *tegra;
	struct tegra_devfreq_device *dev;
	unsigned long target_freq = 0;
	unsigned int i;
	int err;

	err = devfreq->profile->get_dev_status(devfreq->dev.parent, &stat);
	if (err)
		return err;

	tegra = stat.private_data;

	for (i = 0; i < ARRAY_SIZE(tegra->devices); i++) {
		dev = &tegra->devices[i];

		actmon_update_target(tegra, dev);

		target_freq = max(target_freq, dev->target_freq);
	}

	*freq = target_freq;

	return 0;
}

static int tegra_devfreq_event_handler(struct devfreq *devfreq,
				       unsigned int event, void *data)
{
	return 0;
}

static struct devfreq_governor tegra_devfreq_governor = {
	.name = "tegra",
	.get_target_freq = tegra_devfreq_get_target,
	.event_handler = tegra_devfreq_event_handler,
};

static struct devfreq_dev_profile tegra_devfreq_profile = {
	.polling_ms	= 0,
	.target		= tegra_devfreq_target,
	.get_dev_status	= tegra_devfreq_get_dev_status,
};

static int tegra_devfreq_probe(struct platform_device *pdev)
{
	struct tegra_devfreq *tegra;
	struct tegra_devfreq_device *dev;
	struct resource *res;
	unsigned long max_freq;
	unsigned int i;
	int irq;
	int err;

	tegra = devm_kzalloc(&pdev->dev, sizeof(*tegra), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	spin_lock_init(&tegra->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get regs resource\n");
		return -ENODEV;
	}

	tegra->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tegra->regs)) {
		dev_err(&pdev->dev, "Failed to get IO memory\n");
		return PTR_ERR(tegra->regs);
	}

	tegra->reset = devm_reset_control_get(&pdev->dev, "actmon");
	if (IS_ERR(tegra->reset)) {
		dev_err(&pdev->dev, "Failed to get reset\n");
		return PTR_ERR(tegra->reset);
	}

	tegra->clock = devm_clk_get(&pdev->dev, "actmon");
	if (IS_ERR(tegra->clock)) {
		dev_err(&pdev->dev, "Failed to get actmon clock\n");
		return PTR_ERR(tegra->clock);
	}

	tegra->emc_clock = devm_clk_get(&pdev->dev, "emc");
	if (IS_ERR(tegra->emc_clock)) {
		dev_err(&pdev->dev, "Failed to get emc clock\n");
		return PTR_ERR(tegra->emc_clock);
	}

	err = of_init_opp_table(&pdev->dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to init operating point table\n");
		return err;
	}

	tegra->rate_change_nb.notifier_call = tegra_actmon_rate_notify_cb;
	err = clk_notifier_register(tegra->emc_clock, &tegra->rate_change_nb);
	if (err) {
		dev_err(&pdev->dev,
			"Failed to register rate change notifier\n");
		return err;
	}

	reset_control_assert(tegra->reset);

	err = clk_prepare_enable(tegra->clock);
	if (err) {
		reset_control_deassert(tegra->reset);
		return err;
	}

	reset_control_deassert(tegra->reset);

	max_freq = clk_round_rate(tegra->emc_clock, ULONG_MAX);
	tegra->max_freq = max_freq / KHZ;

	clk_set_rate(tegra->emc_clock, max_freq);

	tegra->cur_freq = clk_get_rate(tegra->emc_clock) / KHZ;

	writel(ACTMON_SAMPLING_PERIOD - 1,
	       tegra->regs + ACTMON_GLB_PERIOD_CTRL);

	for (i = 0; i < ARRAY_SIZE(actmon_device_configs); i++) {
		dev = tegra->devices + i;
		dev->config = actmon_device_configs + i;
		dev->regs = tegra->regs + dev->config->offset;

		tegra_actmon_configure_device(tegra, tegra->devices + i);
	}

	err = devfreq_add_governor(&tegra_devfreq_governor);
	if (err) {
		dev_err(&pdev->dev, "Failed to add governor\n");
		return err;
	}

	tegra_devfreq_profile.initial_freq = clk_get_rate(tegra->emc_clock);
	tegra->devfreq = devm_devfreq_add_device(&pdev->dev,
						 &tegra_devfreq_profile,
						 "tegra",
						 NULL);

	irq = platform_get_irq(pdev, 0);
	err = devm_request_threaded_irq(&pdev->dev, irq, actmon_isr,
					actmon_thread_isr, IRQF_SHARED,
					"tegra-devfreq", tegra);
	if (err) {
		dev_err(&pdev->dev, "Interrupt request failed\n");
		return err;
	}

	platform_set_drvdata(pdev, tegra);

	return 0;
}

static int tegra_devfreq_remove(struct platform_device *pdev)
{
	struct tegra_devfreq *tegra = platform_get_drvdata(pdev);

	clk_notifier_unregister(tegra->emc_clock, &tegra->rate_change_nb);

	clk_disable_unprepare(tegra->clock);

	return 0;
}

static SIMPLE_DEV_PM_OPS(tegra_devfreq_pm_ops,
			 tegra_devfreq_suspend,
			 tegra_devfreq_resume);

static struct of_device_id tegra_devfreq_of_match[] = {
	{ .compatible = "nvidia,tegra124-actmon" },
	{ },
};

static struct platform_driver tegra_devfreq_driver = {
	.probe	= tegra_devfreq_probe,
	.remove	= tegra_devfreq_remove,
	.driver = {
		.name		= "tegra-devfreq",
		.owner		= THIS_MODULE,
		.of_match_table = tegra_devfreq_of_match,
		.pm		= &tegra_devfreq_pm_ops,
	},
};
module_platform_driver(tegra_devfreq_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Tegra devfreq driver");
MODULE_AUTHOR("Tomeu Vizoso <tomeu.vizoso@collabora.com>");
MODULE_DEVICE_TABLE(of, tegra_devfreq_of_match);
