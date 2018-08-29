// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018, Fuzhou Rockchip Electronics Co., Ltd
 * Author:  Jeffy Chen <jeffy.chen@rock-chips.com>
 *
 * Base on the Rockchip timer driver drivers/clocksource/rockchip_timer.c by
 * Daniel Lezcano <daniel.lezcano@linaro.org>
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/rtc.h>

#define DRV_NAME		"rk-timer-rtc"

#define TIMER_LOAD_COUNT0	0x00
#define TIMER_LOAD_COUNT1	0x04
#define TIMER_CURRENT_VALUE0	0x08
#define TIMER_CURRENT_VALUE1	0x0C
#define TIMER_CONTROL_REG3288	0x10
#define TIMER_INT_STATUS	0x18

#define TIMER_ENABLE			BIT(0)
#define TIMER_MODE_USER_DEFINED_COUNT	BIT(1)
#define TIMER_INT_UNMASK		BIT(2)

/* Forbid any alarms which would trigger inside the threshold */
#define TIMER_ALARM_THRESHOLD_MS	10

#if !defined(UINT64_MAX)
	#define UINT64_MAX ((u64)-1)
#endif

/**
 * struct rk_timer_rtc_data - Differences between SoC variants
 *
 * @ctrl_reg_offset: The offset of timer control register
 */
struct rk_timer_rtc_data {
	int ctrl_reg_offset;
};

/**
 * struct rk_timer_rtc - Driver data for Rockchip timer RTC
 *
 * @data: Pointer to rk_timer_rtc_data
 * @regmap: Register map of the timer
 * @rtc: Pointer to RTC device
 * @clk: The timer clock
 * @pclk: The peripheral clock
 * @freq: The freq of timer clock
 * @timebase: The base time of the timer RTC
 * @alarm_irq_enabled: Whether to report alarm irqs
 * @irq: The timer IRQ number
 */
struct rk_timer_rtc {
	const struct rk_timer_rtc_data *data;
	struct regmap *regmap;
	struct rtc_device *rtc;
	struct clk *clk;
	struct clk *pclk;
	u32 freq;
	u64 timebase;
	int alarm_irq_enabled;
	int irq;
};

static inline u64 tick_to_sec(struct rk_timer_rtc *rk_timer_rtc, u64 tick)
{
	do_div(tick, rk_timer_rtc->freq);
	return tick;
}

static inline u64 ms_to_tick(struct rk_timer_rtc *rk_timer_rtc, int ms)
{
	return ms * rk_timer_rtc->freq / 1000;
}

static inline u64 tick_to_time64(struct rk_timer_rtc *rk_timer_rtc, u64 tick)
{
	return tick_to_sec(rk_timer_rtc, tick) + rk_timer_rtc->timebase;
}

static inline u64 time64_to_tick(struct rk_timer_rtc *rk_timer_rtc, u64 time)
{
	return (time - rk_timer_rtc->timebase) * rk_timer_rtc->freq;
}

static inline int rk_timer_rtc_write64(struct rk_timer_rtc *rk_timer_rtc,
				       u32 reg, u64 val)
{
	return regmap_bulk_write(rk_timer_rtc->regmap, reg, &val, 2);
}

static inline int rk_timer_rtc_read64(struct rk_timer_rtc *rk_timer_rtc,
				      u32 reg, u64 *val)
{
	u32 val_lo, val_hi, tmp_hi;
	int ret;

	do {
		ret = regmap_read(rk_timer_rtc->regmap, reg + 4, &val_hi);
		if (ret)
			return ret;

		ret = regmap_read(rk_timer_rtc->regmap, reg, &val_lo);
		if (ret)
			return ret;

		ret = regmap_read(rk_timer_rtc->regmap, reg + 4, &tmp_hi);
		if (ret)
			return ret;
	} while (val_hi != tmp_hi);

	*val = ((u64) val_hi << 32) | val_lo;

	return 0;
}

static inline int rk_timer_rtc_irq_clear(struct rk_timer_rtc *rk_timer_rtc)
{
	return regmap_write(rk_timer_rtc->regmap, TIMER_INT_STATUS, 1);
}

static inline int rk_timer_rtc_irq_enable(struct rk_timer_rtc *rk_timer_rtc,
					  unsigned int enabled)
{
	/* Clear any pending irq before enable it */
	if (enabled)
		rk_timer_rtc_irq_clear(rk_timer_rtc);

	return regmap_update_bits(rk_timer_rtc->regmap,
				  rk_timer_rtc->data->ctrl_reg_offset,
				  TIMER_INT_UNMASK,
				  enabled ? TIMER_INT_UNMASK : 0);
}

static int rk_timer_rtc_reset(struct rk_timer_rtc *rk_timer_rtc)
{
	int ret;

	ret = regmap_write(rk_timer_rtc->regmap,
			   rk_timer_rtc->data->ctrl_reg_offset, 0);
	if (ret)
		return ret;

	/* Init load count to UINT64_MAX to keep timer running */
	ret = rk_timer_rtc_write64(rk_timer_rtc, TIMER_LOAD_COUNT0, UINT64_MAX);
	if (ret)
		return ret;

	/* Clear any pending irq before enable it */
	rk_timer_rtc_irq_clear(rk_timer_rtc);

	/* Enable timer in user-defined count mode with irq unmasked */
	return regmap_write(rk_timer_rtc->regmap,
			    rk_timer_rtc->data->ctrl_reg_offset,
			    TIMER_ENABLE | TIMER_MODE_USER_DEFINED_COUNT |
				TIMER_INT_UNMASK);
}

static int rk_timer_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rk_timer_rtc *rk_timer_rtc = dev_get_drvdata(dev);
	int ret;
	u64 tick;

	ret = rk_timer_rtc_read64(rk_timer_rtc, TIMER_CURRENT_VALUE0, &tick);
	if (ret)
		return ret;

	rtc_time64_to_tm(tick_to_time64(rk_timer_rtc, tick), tm);

	dev_dbg(dev, "Read RTC: %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
		1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_wday, tm->tm_hour, tm->tm_min, tm->tm_sec);

	return rtc_valid_tm(tm);
}

static int rk_timer_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rk_timer_rtc *rk_timer_rtc = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "Set RTC:%4d-%02d-%02d(%d) %02d:%02d:%02d\n",
		1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_wday, tm->tm_hour, tm->tm_min, tm->tm_sec);

	ret = rtc_valid_tm(tm);
	if (ret)
		return ret;

	rk_timer_rtc->timebase = rtc_tm_to_time64(tm);

	dev_dbg(dev, "Setting new timebase:%lld\n", rk_timer_rtc->timebase);

	/* Restart timer for new timebase */
	ret = rk_timer_rtc_reset(rk_timer_rtc);
	if (ret) {
		dev_err(dev, "Failed to reset timer:%d\n", ret);
		return ret;
	}

	/* Tell framework to check alarms */
	rtc_update_irq(rk_timer_rtc->rtc, 1, RTC_IRQF | RTC_AF);

	return 0;
}

static int rk_timer_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rk_timer_rtc *rk_timer_rtc = dev_get_drvdata(dev);
	int ret;
	u64 tick;

	ret = rk_timer_rtc_read64(rk_timer_rtc, TIMER_LOAD_COUNT0, &tick);
	if (ret)
		return ret;

	rtc_time64_to_tm(tick_to_time64(rk_timer_rtc, tick), &alrm->time);

	dev_dbg(dev, "Read alarm: %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
		1900 + alrm->time.tm_year, alrm->time.tm_mon + 1,
		alrm->time.tm_mday, alrm->time.tm_wday, alrm->time.tm_hour,
		alrm->time.tm_min, alrm->time.tm_sec);

	return rtc_valid_tm(&alrm->time);
}

static int rk_timer_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rk_timer_rtc *rk_timer_rtc = dev_get_drvdata(dev);
	int ret;
	u64 alarm_tick, alarm_threshold_tick, cur_tick;

	dev_dbg(dev, "Set alarm:%4d-%02d-%02d(%d) %02d:%02d:%02d\n",
		1900 + alrm->time.tm_year, alrm->time.tm_mon + 1,
		alrm->time.tm_mday, alrm->time.tm_wday, alrm->time.tm_hour,
		alrm->time.tm_min, alrm->time.tm_sec);

	ret = rtc_valid_tm(&alrm->time);
	if (ret)
		return ret;

	rk_timer_rtc->alarm_irq_enabled = false;

	alarm_tick = time64_to_tick(rk_timer_rtc,
				    rtc_tm_to_time64(&alrm->time));

	ret = rk_timer_rtc_read64(rk_timer_rtc, TIMER_CURRENT_VALUE0,
				  &cur_tick);
	if (ret)
		return ret;

	/* Don't set an alarm in the past or about to pass */
	alarm_threshold_tick = ms_to_tick(rk_timer_rtc,
					  TIMER_ALARM_THRESHOLD_MS);
	if (alarm_tick <= (cur_tick + alarm_threshold_tick))
		return -ETIME;

	/*
	 * When the current value counts up to the load count, the timer will
	 * stop and generate an irq.
	 */
	ret = rk_timer_rtc_write64(rk_timer_rtc, TIMER_LOAD_COUNT0, alarm_tick);
	if (ret)
		return ret;

	dev_dbg(dev, "New alarm enabled:%d\n", alrm->enabled);
	rk_timer_rtc->alarm_irq_enabled = alrm->enabled;

	return 0;
}

static int rk_timer_rtc_alarm_irq_enable(struct device *dev,
					 unsigned int enabled)
{
	struct rk_timer_rtc *rk_timer_rtc = dev_get_drvdata(dev);

	dev_dbg(dev, "Set alarm irq enabled:%d\n", enabled);
	rk_timer_rtc->alarm_irq_enabled = enabled;

	return 0;
}

static irqreturn_t rk_timer_rtc_alarm_irq(int irq, void *data)
{
	struct device *dev = data;
	struct rk_timer_rtc *rk_timer_rtc = dev_get_drvdata(dev);
	int ret;

	dev_dbg(dev, "Received timer irq, alarm_irq_enabled:%d\n",
		rk_timer_rtc->alarm_irq_enabled);

	/* The timer is stopped now, reset the load count to start it again */
	ret = rk_timer_rtc_write64(rk_timer_rtc, TIMER_LOAD_COUNT0, UINT64_MAX);
	if (ret)
		dev_err(dev, "Failed to set load count:%d\n", ret);

	ret = regmap_write(rk_timer_rtc->regmap, TIMER_INT_STATUS, 1);
	if (ret)
		dev_err(dev, "Failed to clear irq:%d\n", ret);

	/* Only report rtc irq when alarm irq is enabled */
	if (rk_timer_rtc->alarm_irq_enabled)
		rtc_update_irq(rk_timer_rtc->rtc, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops rk_timer_rtc_ops = {
	.read_time = rk_timer_rtc_read_time,
	.set_time = rk_timer_rtc_set_time,
	.read_alarm = rk_timer_rtc_read_alarm,
	.set_alarm = rk_timer_rtc_set_alarm,
	.alarm_irq_enable = rk_timer_rtc_alarm_irq_enable,
};

static struct regmap_config rk_timer_regmap_config = {
	.name		= DRV_NAME,
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
};

static const struct of_device_id rk_timer_rtc_dt_match[];

static int rk_timer_rtc_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct rk_timer_rtc *rk_timer_rtc;
	void __iomem *base;
	resource_size_t size;
	int ret;

	rk_timer_rtc = devm_kzalloc(dev, sizeof(*rk_timer_rtc), GFP_KERNEL);
	if (!rk_timer_rtc)
		return -ENOMEM;

	match = of_match_node(rk_timer_rtc_dt_match, dev->of_node);
	rk_timer_rtc->data = match->data;

	platform_set_drvdata(pdev, rk_timer_rtc);

	base = devm_of_iomap(dev, dev->of_node, 0, &size);
	if (!base) {
		dev_err(dev, "Failed to iomap\n");
		return -EINVAL;
	}

	rk_timer_regmap_config.max_register = size - 4;
	rk_timer_rtc->regmap = devm_regmap_init_mmio(dev, base,
						     &rk_timer_regmap_config);
	if (IS_ERR(rk_timer_rtc->regmap)) {
		ret = PTR_ERR(rk_timer_rtc->regmap);
		dev_err(dev, "Failed to init regmap:%d\n", ret);
		return ret;
	}

	rk_timer_rtc->irq = platform_get_irq(pdev, 0);
	if (rk_timer_rtc->irq < 0) {
		ret = rk_timer_rtc->irq;
		dev_err(dev, "Failed to get irq:%d\n", ret);
		return ret;
	}

	ret = devm_request_irq(dev, rk_timer_rtc->irq, rk_timer_rtc_alarm_irq,
			       0, dev_name(dev), dev);
	if (ret) {
		dev_err(dev, "Failed to request irq:%d\n", ret);
		return ret;
	}

	rk_timer_rtc->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(rk_timer_rtc->pclk)) {
		ret = PTR_ERR(rk_timer_rtc->pclk);
		pr_err("Failed to get timer pclk:%d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(rk_timer_rtc->pclk);
	if (ret) {
		dev_err(dev, "Failed to enable pclk:%d\n", ret);
		return ret;
	}

	rk_timer_rtc->clk = devm_clk_get(dev, "timer");
	if (IS_ERR(rk_timer_rtc->clk)) {
		ret = PTR_ERR(rk_timer_rtc->clk);
		pr_err("Failed to get timer clk:%d\n", ret);
		goto err_disable_pclk;
	}

	ret = clk_prepare_enable(rk_timer_rtc->clk);
	if (ret) {
		dev_err(dev, "Failed to enable timer clk:%d\n", ret);
		goto err_disable_pclk;
	}

	rk_timer_rtc->freq = clk_get_rate(rk_timer_rtc->clk);
	dev_dbg(dev, "RTC timer freq:%d\n", rk_timer_rtc->freq);

	ret = rk_timer_rtc_reset(rk_timer_rtc);
	if (ret) {
		dev_err(dev, "Failed to reset timer:%d\n", ret);
		goto err_disable_clk;
	}

	ret = device_init_wakeup(dev, true);
	if (ret) {
		dev_err(dev, "Failed to init wakeup:%d\n", ret);
		goto err_disable_irq;
	}

	rk_timer_rtc->rtc = devm_rtc_device_register(dev, DRV_NAME,
						     &rk_timer_rtc_ops,
						     THIS_MODULE);
	if (IS_ERR(rk_timer_rtc->rtc)) {
		ret = PTR_ERR(rk_timer_rtc->rtc);
		dev_err(dev, "Failed to register rtc:%d\n", ret);
		goto err_uninit_wakeup;
	}

	return 0;
err_uninit_wakeup:
	device_init_wakeup(&pdev->dev, false);
err_disable_irq:
	rk_timer_rtc_irq_enable(rk_timer_rtc, false);
err_disable_clk:
	clk_disable_unprepare(rk_timer_rtc->clk);
err_disable_pclk:
	clk_disable_unprepare(rk_timer_rtc->pclk);
	return ret;
}

static int rk_timer_rtc_remove(struct platform_device *pdev)
{
	struct rk_timer_rtc *rk_timer_rtc = dev_get_drvdata(&pdev->dev);

	device_init_wakeup(&pdev->dev, false);
	rk_timer_rtc_irq_enable(rk_timer_rtc, false);
	clk_disable_unprepare(rk_timer_rtc->clk);
	clk_disable_unprepare(rk_timer_rtc->pclk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rk_timer_rtc_suspend(struct device *dev)
{
	struct rk_timer_rtc *rk_timer_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(rk_timer_rtc->irq);

	return 0;
}

static int rk_timer_rtc_resume(struct device *dev)
{
	struct rk_timer_rtc *rk_timer_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(rk_timer_rtc->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(rk_timer_rtc_pm_ops, rk_timer_rtc_suspend,
			 rk_timer_rtc_resume);

static const struct rk_timer_rtc_data rk3288_timer_rtc_data = {
	.ctrl_reg_offset = TIMER_CONTROL_REG3288,
};

static const struct of_device_id rk_timer_rtc_dt_match[] = {
	{
		.compatible = "rockchip,rk3308-timer-rtc",
		.data = &rk3288_timer_rtc_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, rk_timer_rtc_dt_match);

static struct platform_driver rk_timer_rtc_driver = {
	.probe = rk_timer_rtc_probe,
	.remove = rk_timer_rtc_remove,
	.driver = {
		.name = DRV_NAME,
		.pm = &rk_timer_rtc_pm_ops,
		.of_match_table = of_match_ptr(rk_timer_rtc_dt_match),
	},
};

module_platform_driver(rk_timer_rtc_driver);

MODULE_DESCRIPTION("RTC driver for the rockchip timer");
MODULE_AUTHOR("Jeffy Chen <jeffy.chen@rock-chips.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
