// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2014-2017 Broadcom
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_wakeup.h>
#include <linux/reboot.h>
#include <linux/rtc.h>
#include <linux/stat.h>
#include <linux/suspend.h>

struct brcmstb_waketmr {
	struct rtc_device *rtc;
	struct device *dev;
	void __iomem *base;
	int irq;
	struct notifier_block reboot_notifier;
	struct clk *clk;
	u32 rate;
};

#define BRCMSTB_WKTMR_EVENT		0x00
#define BRCMSTB_WKTMR_COUNTER		0x04
#define BRCMSTB_WKTMR_ALARM		0x08
#define BRCMSTB_WKTMR_PRESCALER		0x0C
#define BRCMSTB_WKTMR_PRESCALER_VAL	0x10

#define BRCMSTB_WKTMR_DEFAULT_FREQ	27000000

static inline void brcmstb_waketmr_clear_alarm(struct brcmstb_waketmr *timer)
{
	writel_relaxed(1, timer->base + BRCMSTB_WKTMR_EVENT);
	(void)readl_relaxed(timer->base + BRCMSTB_WKTMR_EVENT);
}

static void brcmstb_waketmr_set_alarm(struct brcmstb_waketmr *timer,
				      unsigned int secs)
{
	brcmstb_waketmr_clear_alarm(timer);

	/* Make sure we are actually counting in seconds */
	writel_relaxed(timer->rate, timer->base + BRCMSTB_WKTMR_PRESCALER);

	writel_relaxed(secs + 1, timer->base + BRCMSTB_WKTMR_ALARM);
}

static irqreturn_t brcmstb_waketmr_irq(int irq, void *data)
{
	struct brcmstb_waketmr *timer = data;

	pm_wakeup_event(timer->dev, 0);

	return IRQ_HANDLED;
}

struct wktmr_time {
	u32 sec;
	u32 pre;
};

static void wktmr_read(struct brcmstb_waketmr *timer,
		       struct wktmr_time *t)
{
	u32 tmp;

	do {
		t->sec = readl_relaxed(timer->base + BRCMSTB_WKTMR_COUNTER);
		tmp = readl_relaxed(timer->base + BRCMSTB_WKTMR_PRESCALER_VAL);
	} while (tmp >= timer->rate);

	t->pre = timer->rate - tmp;
}

static int brcmstb_waketmr_prepare_suspend(struct brcmstb_waketmr *timer)
{
	struct device *dev = timer->dev;
	int ret = 0;

	if (device_may_wakeup(dev)) {
		ret = enable_irq_wake(timer->irq);
		if (ret) {
			dev_err(dev, "failed to enable wake-up interrupt\n");
			return ret;
		}
	}

	return ret;
}

/* If enabled as a wakeup-source, arm the timer when powering off */
static int brcmstb_waketmr_reboot(struct notifier_block *nb,
		unsigned long action, void *data)
{
	struct brcmstb_waketmr *timer;

	timer = container_of(nb, struct brcmstb_waketmr, reboot_notifier);

	/* Set timer for cold boot */
	if (action == SYS_POWER_OFF)
		brcmstb_waketmr_prepare_suspend(timer);

	return NOTIFY_DONE;
}

static int brcmstb_waketmr_gettime(struct device *dev,
				   struct rtc_time *tm)
{
	struct brcmstb_waketmr *timer = dev_get_drvdata(dev);
	struct wktmr_time now;

	wktmr_read(timer, &now);

	rtc_time64_to_tm(now.sec, tm);

	return 0;
}

static int brcmstb_waketmr_settime(struct device *dev,
				   struct rtc_time *tm)
{
	struct brcmstb_waketmr *timer = dev_get_drvdata(dev);
	time64_t sec;

	sec = rtc_tm_to_time64(tm);

	writel_relaxed(sec, timer->base + BRCMSTB_WKTMR_COUNTER);

	return 0;
}

static int brcmstb_waketmr_getalarm(struct device *dev,
				    struct rtc_wkalrm *alarm)
{
	struct brcmstb_waketmr *timer = dev_get_drvdata(dev);
	time64_t sec;
	u32 reg;

	sec = readl_relaxed(timer->base + BRCMSTB_WKTMR_ALARM);
	if (sec != 0) {
		/* Alarm is enabled */
		alarm->enabled = 1;
		rtc_time64_to_tm(sec, &alarm->time);
	}

	reg = readl_relaxed(timer->base + BRCMSTB_WKTMR_EVENT);
	alarm->pending = !!(reg & 1);

	return 0;
}

static int brcmstb_waketmr_setalarm(struct device *dev,
				     struct rtc_wkalrm *alarm)
{
	struct brcmstb_waketmr *timer = dev_get_drvdata(dev);
	time64_t sec;

	if (alarm->enabled)
		sec = rtc_tm_to_time64(&alarm->time);
	else
		sec = 0;

	brcmstb_waketmr_set_alarm(timer, sec);

	return 0;
}

/*
 * Does not do much but keep the RTC class happy. We always support
 * alarms.
 */
static int brcmstb_waketmr_alarm_enable(struct device *dev,
					unsigned int enabled)
{
	return 0;
}

static const struct rtc_class_ops brcmstb_waketmr_ops = {
	.read_time	= brcmstb_waketmr_gettime,
	.set_time	= brcmstb_waketmr_settime,
	.read_alarm	= brcmstb_waketmr_getalarm,
	.set_alarm	= brcmstb_waketmr_setalarm,
	.alarm_irq_enable = brcmstb_waketmr_alarm_enable,
};

static int brcmstb_waketmr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct brcmstb_waketmr *timer;
	int ret;

	timer = devm_kzalloc(dev, sizeof(*timer), GFP_KERNEL);
	if (!timer)
		return -ENOMEM;

	platform_set_drvdata(pdev, timer);
	timer->dev = dev;

	timer->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(timer->base))
		return PTR_ERR(timer->base);

	timer->rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(timer->rtc))
		return PTR_ERR(timer->rtc);

	/*
	 * Set wakeup capability before requesting wakeup interrupt, so we can
	 * process boot-time "wakeups" (e.g., from S5 soft-off)
	 */
	device_set_wakeup_capable(dev, true);
	device_wakeup_enable(dev);

	timer->irq = platform_get_irq(pdev, 0);
	if (timer->irq < 0)
		return -ENODEV;

	timer->clk = devm_clk_get(dev, NULL);
	if (!IS_ERR(timer->clk)) {
		ret = clk_prepare_enable(timer->clk);
		if (ret)
			return ret;
		timer->rate = clk_get_rate(timer->clk);
		if (!timer->rate)
			timer->rate = BRCMSTB_WKTMR_DEFAULT_FREQ;
	} else {
		timer->rate = BRCMSTB_WKTMR_DEFAULT_FREQ;
		timer->clk = NULL;
	}

	ret = devm_request_irq(dev, timer->irq, brcmstb_waketmr_irq, 0,
			       "brcmstb-waketimer", timer);
	if (ret < 0)
		goto err_clk;

	timer->reboot_notifier.notifier_call = brcmstb_waketmr_reboot;
	register_reboot_notifier(&timer->reboot_notifier);

	timer->rtc->ops = &brcmstb_waketmr_ops;
	timer->rtc->range_max = U32_MAX;

	ret = devm_rtc_register_device(timer->rtc);
	if (ret)
		goto err_notifier;

	dev_info(dev, "registered, with irq %d\n", timer->irq);

	return 0;

err_notifier:
	unregister_reboot_notifier(&timer->reboot_notifier);

err_clk:
	clk_disable_unprepare(timer->clk);

	return ret;
}

static int brcmstb_waketmr_remove(struct platform_device *pdev)
{
	struct brcmstb_waketmr *timer = dev_get_drvdata(&pdev->dev);

	unregister_reboot_notifier(&timer->reboot_notifier);
	clk_disable_unprepare(timer->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int brcmstb_waketmr_suspend(struct device *dev)
{
	struct brcmstb_waketmr *timer = dev_get_drvdata(dev);

	return brcmstb_waketmr_prepare_suspend(timer);
}

static int brcmstb_waketmr_resume(struct device *dev)
{
	struct brcmstb_waketmr *timer = dev_get_drvdata(dev);
	int ret;

	if (!device_may_wakeup(dev))
		return 0;

	ret = disable_irq_wake(timer->irq);

	brcmstb_waketmr_clear_alarm(timer);

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(brcmstb_waketmr_pm_ops,
			 brcmstb_waketmr_suspend, brcmstb_waketmr_resume);

static const __maybe_unused struct of_device_id brcmstb_waketmr_of_match[] = {
	{ .compatible = "brcm,brcmstb-waketimer" },
	{ /* sentinel */ },
};

static struct platform_driver brcmstb_waketmr_driver = {
	.probe			= brcmstb_waketmr_probe,
	.remove			= brcmstb_waketmr_remove,
	.driver = {
		.name		= "brcmstb-waketimer",
		.pm		= &brcmstb_waketmr_pm_ops,
		.of_match_table	= of_match_ptr(brcmstb_waketmr_of_match),
	}
};
module_platform_driver(brcmstb_waketmr_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Brian Norris");
MODULE_AUTHOR("Markus Mayer");
MODULE_DESCRIPTION("Wake-up timer driver for STB chips");
