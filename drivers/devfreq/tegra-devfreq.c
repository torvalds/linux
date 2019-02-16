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
#include <linux/mod_devicetable.h>
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

/*
 * Activity counter is incremented every 256 memory transactions, and each
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
 * Coefficients and thresholds are percentages unless otherwise noted
 */
struct tegra_devfreq_device_config {
	u32		offset;
	u32		irq_mask;

	/* Factors applied to boost_freq every consecutive watermark breach */
	unsigned int	boost_up_coeff;
	unsigned int	boost_down_coeff;

	/* Define the watermark bounds when applied to the current avg */
	unsigned int	boost_up_threshold;
	unsigned int	boost_down_threshold;

	/*
	 * Threshold of activity (cycles) below which the CPU frequency isn't
	 * to be taken into account. This is to avoid increasing the EMC
	 * frequency when the CPU is very busy but not accessing the bus often.
	 */
	u32		avg_dependency_threshold;
};

enum tegra_actmon_device {
	MCALL = 0,
	MCCPU,
};

static struct tegra_devfreq_device_config actmon_device_configs[] = {
	{
		/* MCALL: All memory accesses (including from the CPUs) */
		.offset = 0x1c0,
		.irq_mask = 1 << 26,
		.boost_up_coeff = 200,
		.boost_down_coeff = 50,
		.boost_up_threshold = 60,
		.boost_down_threshold = 40,
	},
	{
		/* MCCPU: memory accesses from the CPUs */
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
	void __iomem *regs;
	spinlock_t lock;

	/* Average event count sampled in the last interrupt */
	u32 avg_count;

	/*
	 * Extra frequency to increase the target by due to consecutive
	 * watermark breaches.
	 */
	unsigned long boost_freq;

	/* Optimal frequency calculated from the stats for this device */
	unsigned long target_freq;
};

struct tegra_devfreq {
	struct devfreq		*devfreq;

	struct reset_control	*reset;
	struct clk		*clock;
	void __iomem		*regs;

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

static u32 actmon_readl(struct tegra_devfreq *tegra, u32 offset)
{
	return readl(tegra->regs + offset);
}

static void actmon_writel(struct tegra_devfreq *tegra, u32 val, u32 offset)
{
	writel(val, tegra->regs + offset);
}

static u32 device_readl(struct tegra_devfreq_device *dev, u32 offset)
{
	return readl(dev->regs + offset);
}

static void device_writel(struct tegra_devfreq_device *dev, u32 val,
			  u32 offset)
{
	writel(val, dev->regs + offset);
}

static unsigned long do_percent(unsigned long val, unsigned int pct)
{
	return val * pct / 100;
}

static void tegra_devfreq_update_avg_wmark(struct tegra_devfreq *tegra,
					   struct tegra_devfreq_device *dev)
{
	u32 avg = dev->avg_count;
	u32 avg_band_freq = tegra->max_freq * ACTMON_DEFAULT_AVG_BAND / KHZ;
	u32 band = avg_band_freq * ACTMON_SAMPLING_PERIOD;

	device_writel(dev, avg + band, ACTMON_DEV_AVG_UPPER_WMARK);

	avg = max(dev->avg_count, band);
	device_writel(dev, avg - band, ACTMON_DEV_AVG_LOWER_WMARK);
}

static void tegra_devfreq_update_wmark(struct tegra_devfreq *tegra,
				       struct tegra_devfreq_device *dev)
{
	u32 val = tegra->cur_freq * ACTMON_SAMPLING_PERIOD;

	device_writel(dev, do_percent(val, dev->config->boost_up_threshold),
		      ACTMON_DEV_UPPER_WMARK);

	device_writel(dev, do_percent(val, dev->config->boost_down_threshold),
		      ACTMON_DEV_LOWER_WMARK);
}

static void actmon_write_barrier(struct tegra_devfreq *tegra)
{
	/* ensure the update has reached the ACTMON */
	wmb();
	actmon_readl(tegra, ACTMON_GLB_STATUS);
}

static void actmon_isr_device(struct tegra_devfreq *tegra,
			      struct tegra_devfreq_device *dev)
{
	unsigned long flags;
	u32 intr_status, dev_ctrl;

	spin_lock_irqsave(&dev->lock, flags);

	dev->avg_count = device_readl(dev, ACTMON_DEV_AVG_COUNT);
	tegra_devfreq_update_avg_wmark(tegra, dev);

	intr_status = device_readl(dev, ACTMON_DEV_INTR_STATUS);
	dev_ctrl = device_readl(dev, ACTMON_DEV_CTRL);

	if (intr_status & ACTMON_DEV_INTR_CONSECUTIVE_UPPER) {
		/*
		 * new_boost = min(old_boost * up_coef + step, max_freq)
		 */
		dev->boost_freq = do_percent(dev->boost_freq,
					     dev->config->boost_up_coeff);
		dev->boost_freq += ACTMON_BOOST_FREQ_STEP;

		dev_ctrl |= ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_EN;

		if (dev->boost_freq >= tegra->max_freq)
			dev->boost_freq = tegra->max_freq;
		else
			dev_ctrl |= ACTMON_DEV_CTRL_CONSECUTIVE_ABOVE_WMARK_EN;
	} else if (intr_status & ACTMON_DEV_INTR_CONSECUTIVE_LOWER) {
		/*
		 * new_boost = old_boost * down_coef
		 * or 0 if (old_boost * down_coef < step / 2)
		 */
		dev->boost_freq = do_percent(dev->boost_freq,
					     dev->config->boost_down_coeff);

		dev_ctrl |= ACTMON_DEV_CTRL_CONSECUTIVE_ABOVE_WMARK_EN;

		if (dev->boost_freq < (ACTMON_BOOST_FREQ_STEP >> 1))
			dev->boost_freq = 0;
		else
			dev_ctrl |= ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_EN;
	}

	if (dev->config->avg_dependency_threshold) {
		if (dev->avg_count >= dev->config->avg_dependency_threshold)
			dev_ctrl |= ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_EN;
		else if (dev->boost_freq == 0)
			dev_ctrl &= ~ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_EN;
	}

	device_writel(dev, dev_ctrl, ACTMON_DEV_CTRL);

	device_writel(dev, ACTMON_INTR_STATUS_CLEAR, ACTMON_DEV_INTR_STATUS);

	actmon_write_barrier(tegra);

	spin_unlock_irqrestore(&dev->lock, flags);
}

static irqreturn_t actmon_isr(int irq, void *data)
{
	struct tegra_devfreq *tegra = data;
	bool handled = false;
	unsigned int i;
	u32 val;

	val = actmon_readl(tegra, ACTMON_GLB_STATUS);
	for (i = 0; i < ARRAY_SIZE(tegra->devices); i++) {
		if (val & tegra->devices[i].config->irq_mask) {
			actmon_isr_device(tegra, tegra->devices + i);
			handled = true;
		}
	}

	return handled ? IRQ_WAKE_THREAD : IRQ_NONE;
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

	spin_lock_irqsave(&dev->lock, flags);

	dev->target_freq = dev->avg_count / ACTMON_SAMPLING_PERIOD;
	avg_sustain_coef = 100 * 100 / dev->config->boost_up_threshold;
	dev->target_freq = do_percent(dev->target_freq, avg_sustain_coef);
	dev->target_freq += dev->boost_freq;

	if (dev->avg_count >= dev->config->avg_dependency_threshold)
		dev->target_freq = max(dev->target_freq, static_cpu_emc_freq);

	spin_unlock_irqrestore(&dev->lock, flags);
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
	struct tegra_devfreq *tegra;
	struct tegra_devfreq_device *dev;
	unsigned int i;
	unsigned long flags;

	if (action != POST_RATE_CHANGE)
		return NOTIFY_OK;

	tegra = container_of(nb, struct tegra_devfreq, rate_change_nb);

	tegra->cur_freq = data->new_rate / KHZ;

	for (i = 0; i < ARRAY_SIZE(tegra->devices); i++) {
		dev = &tegra->devices[i];

		spin_lock_irqsave(&dev->lock, flags);
		tegra_devfreq_update_wmark(tegra, dev);
		spin_unlock_irqrestore(&dev->lock, flags);
	}

	actmon_write_barrier(tegra);

	return NOTIFY_OK;
}

static void tegra_actmon_enable_interrupts(struct tegra_devfreq *tegra)
{
	struct tegra_devfreq_device *dev;
	u32 val;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(tegra->devices); i++) {
		dev = &tegra->devices[i];

		val = device_readl(dev, ACTMON_DEV_CTRL);
		val |= ACTMON_DEV_CTRL_AVG_ABOVE_WMARK_EN;
		val |= ACTMON_DEV_CTRL_AVG_BELOW_WMARK_EN;
		val |= ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_EN;
		val |= ACTMON_DEV_CTRL_CONSECUTIVE_ABOVE_WMARK_EN;

		device_writel(dev, val, ACTMON_DEV_CTRL);
	}

	actmon_write_barrier(tegra);
}

static void tegra_actmon_disable_interrupts(struct tegra_devfreq *tegra)
{
	struct tegra_devfreq_device *dev;
	u32 val;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(tegra->devices); i++) {
		dev = &tegra->devices[i];

		val = device_readl(dev, ACTMON_DEV_CTRL);
		val &= ~ACTMON_DEV_CTRL_AVG_ABOVE_WMARK_EN;
		val &= ~ACTMON_DEV_CTRL_AVG_BELOW_WMARK_EN;
		val &= ~ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_EN;
		val &= ~ACTMON_DEV_CTRL_CONSECUTIVE_ABOVE_WMARK_EN;

		device_writel(dev, val, ACTMON_DEV_CTRL);
	}

	actmon_write_barrier(tegra);
}

static void tegra_actmon_configure_device(struct tegra_devfreq *tegra,
					  struct tegra_devfreq_device *dev)
{
	u32 val = 0;

	dev->target_freq = tegra->cur_freq;

	dev->avg_count = tegra->cur_freq * ACTMON_SAMPLING_PERIOD;
	device_writel(dev, dev->avg_count, ACTMON_DEV_INIT_AVG);

	tegra_devfreq_update_avg_wmark(tegra, dev);
	tegra_devfreq_update_wmark(tegra, dev);

	device_writel(dev, ACTMON_COUNT_WEIGHT, ACTMON_DEV_COUNT_WEIGHT);
	device_writel(dev, ACTMON_INTR_STATUS_CLEAR, ACTMON_DEV_INTR_STATUS);

	val |= ACTMON_DEV_CTRL_ENB_PERIODIC;
	val |= (ACTMON_AVERAGE_WINDOW_LOG2 - 1)
		<< ACTMON_DEV_CTRL_K_VAL_SHIFT;
	val |= (ACTMON_BELOW_WMARK_WINDOW - 1)
		<< ACTMON_DEV_CTRL_CONSECUTIVE_BELOW_WMARK_NUM_SHIFT;
	val |= (ACTMON_ABOVE_WMARK_WINDOW - 1)
		<< ACTMON_DEV_CTRL_CONSECUTIVE_ABOVE_WMARK_NUM_SHIFT;
	val |= ACTMON_DEV_CTRL_ENB;

	device_writel(dev, val, ACTMON_DEV_CTRL);

	actmon_write_barrier(tegra);
}

static int tegra_devfreq_target(struct device *dev, unsigned long *freq,
				u32 flags)
{
	struct tegra_devfreq *tegra = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	unsigned long rate = *freq * KHZ;

	opp = devfreq_recommended_opp(dev, &rate, flags);
	if (IS_ERR(opp)) {
		dev_err(dev, "Failed to find opp for %lu KHz\n", *freq);
		return PTR_ERR(opp);
	}
	rate = dev_pm_opp_get_freq(opp);
	dev_pm_opp_put(opp);

	clk_set_min_rate(tegra->emc_clock, rate);
	clk_set_rate(tegra->emc_clock, 0);

	*freq = rate;

	return 0;
}

static int tegra_devfreq_get_dev_status(struct device *dev,
					struct devfreq_dev_status *stat)
{
	struct tegra_devfreq *tegra = dev_get_drvdata(dev);
	struct tegra_devfreq_device *actmon_dev;

	stat->current_frequency = tegra->cur_freq;

	/* To be used by the tegra governor */
	stat->private_data = tegra;

	/* The below are to be used by the other governors */

	actmon_dev = &tegra->devices[MCALL];

	/* Number of cycles spent on memory access */
	stat->busy_time = device_readl(actmon_dev, ACTMON_DEV_AVG_COUNT);

	/* The bus can be considered to be saturated way before 100% */
	stat->busy_time *= 100 / BUS_SATURATION_RATIO;

	/* Number of cycles in a sampling period */
	stat->total_time = ACTMON_SAMPLING_PERIOD * tegra->cur_freq;

	stat->busy_time = min(stat->busy_time, stat->total_time);

	return 0;
}

static struct devfreq_dev_profile tegra_devfreq_profile = {
	.polling_ms	= 0,
	.target		= tegra_devfreq_target,
	.get_dev_status	= tegra_devfreq_get_dev_status,
};

static int tegra_governor_get_target(struct devfreq *devfreq,
				     unsigned long *freq)
{
	struct devfreq_dev_status *stat;
	struct tegra_devfreq *tegra;
	struct tegra_devfreq_device *dev;
	unsigned long target_freq = 0;
	unsigned int i;
	int err;

	err = devfreq_update_stats(devfreq);
	if (err)
		return err;

	stat = &devfreq->last_status;

	tegra = stat->private_data;

	for (i = 0; i < ARRAY_SIZE(tegra->devices); i++) {
		dev = &tegra->devices[i];

		actmon_update_target(tegra, dev);

		target_freq = max(target_freq, dev->target_freq);
	}

	*freq = target_freq;

	return 0;
}

static int tegra_governor_event_handler(struct devfreq *devfreq,
					unsigned int event, void *data)
{
	struct tegra_devfreq *tegra = dev_get_drvdata(devfreq->dev.parent);

	switch (event) {
	case DEVFREQ_GOV_START:
		devfreq_monitor_start(devfreq);
		tegra_actmon_enable_interrupts(tegra);
		break;

	case DEVFREQ_GOV_STOP:
		tegra_actmon_disable_interrupts(tegra);
		devfreq_monitor_stop(devfreq);
		break;

	case DEVFREQ_GOV_SUSPEND:
		tegra_actmon_disable_interrupts(tegra);
		devfreq_monitor_suspend(devfreq);
		break;

	case DEVFREQ_GOV_RESUME:
		devfreq_monitor_resume(devfreq);
		tegra_actmon_enable_interrupts(tegra);
		break;
	}

	return 0;
}

static struct devfreq_governor tegra_devfreq_governor = {
	.name = "tegra_actmon",
	.get_target_freq = tegra_governor_get_target,
	.event_handler = tegra_governor_event_handler,
};

static int tegra_devfreq_probe(struct platform_device *pdev)
{
	struct tegra_devfreq *tegra;
	struct tegra_devfreq_device *dev;
	struct resource *res;
	unsigned int i;
	unsigned long rate;
	int irq;
	int err;

	tegra = devm_kzalloc(&pdev->dev, sizeof(*tegra), GFP_KERNEL);
	if (!tegra)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	tegra->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tegra->regs))
		return PTR_ERR(tegra->regs);

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

	clk_set_rate(tegra->emc_clock, ULONG_MAX);

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
		dev_err(&pdev->dev,
			"Failed to prepare and enable ACTMON clock\n");
		return err;
	}

	reset_control_deassert(tegra->reset);

	tegra->max_freq = clk_round_rate(tegra->emc_clock, ULONG_MAX) / KHZ;
	tegra->cur_freq = clk_get_rate(tegra->emc_clock) / KHZ;

	actmon_writel(tegra, ACTMON_SAMPLING_PERIOD - 1,
		      ACTMON_GLB_PERIOD_CTRL);

	for (i = 0; i < ARRAY_SIZE(actmon_device_configs); i++) {
		dev = tegra->devices + i;
		dev->config = actmon_device_configs + i;
		dev->regs = tegra->regs + dev->config->offset;
		spin_lock_init(&dev->lock);

		tegra_actmon_configure_device(tegra, dev);
	}

	for (rate = 0; rate <= tegra->max_freq * KHZ; rate++) {
		rate = clk_round_rate(tegra->emc_clock, rate);
		dev_pm_opp_add(&pdev->dev, rate, 0);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "Failed to get IRQ: %d\n", irq);
		return irq;
	}

	platform_set_drvdata(pdev, tegra);

	err = devm_request_threaded_irq(&pdev->dev, irq, actmon_isr,
					actmon_thread_isr, IRQF_SHARED,
					"tegra-devfreq", tegra);
	if (err) {
		dev_err(&pdev->dev, "Interrupt request failed\n");
		return err;
	}

	tegra_devfreq_profile.initial_freq = clk_get_rate(tegra->emc_clock);
	tegra->devfreq = devm_devfreq_add_device(&pdev->dev,
						 &tegra_devfreq_profile,
						 "tegra_actmon",
						 NULL);

	return 0;
}

static int tegra_devfreq_remove(struct platform_device *pdev)
{
	struct tegra_devfreq *tegra = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);
	u32 val;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(actmon_device_configs); i++) {
		val = device_readl(&tegra->devices[i], ACTMON_DEV_CTRL);
		val &= ~ACTMON_DEV_CTRL_ENB;
		device_writel(&tegra->devices[i], val, ACTMON_DEV_CTRL);
	}

	actmon_write_barrier(tegra);

	devm_free_irq(&pdev->dev, irq, tegra);

	clk_notifier_unregister(tegra->emc_clock, &tegra->rate_change_nb);

	clk_disable_unprepare(tegra->clock);

	return 0;
}

static const struct of_device_id tegra_devfreq_of_match[] = {
	{ .compatible = "nvidia,tegra124-actmon" },
	{ },
};

MODULE_DEVICE_TABLE(of, tegra_devfreq_of_match);

static struct platform_driver tegra_devfreq_driver = {
	.probe	= tegra_devfreq_probe,
	.remove	= tegra_devfreq_remove,
	.driver = {
		.name = "tegra-devfreq",
		.of_match_table = tegra_devfreq_of_match,
	},
};

static int __init tegra_devfreq_init(void)
{
	int ret = 0;

	ret = devfreq_add_governor(&tegra_devfreq_governor);
	if (ret) {
		pr_err("%s: failed to add governor: %d\n", __func__, ret);
		return ret;
	}

	ret = platform_driver_register(&tegra_devfreq_driver);
	if (ret)
		devfreq_remove_governor(&tegra_devfreq_governor);

	return ret;
}
module_init(tegra_devfreq_init)

static void __exit tegra_devfreq_exit(void)
{
	int ret = 0;

	platform_driver_unregister(&tegra_devfreq_driver);

	ret = devfreq_remove_governor(&tegra_devfreq_governor);
	if (ret)
		pr_err("%s: failed to remove governor: %d\n", __func__, ret);
}
module_exit(tegra_devfreq_exit)

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Tegra devfreq driver");
MODULE_AUTHOR("Tomeu Vizoso <tomeu.vizoso@collabora.com>");
